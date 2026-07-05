# Agent 暂停后页面卡死 — 分析报告

## 1. 现象

知识库页面与 Agent 对话过程中，点击停止按钮（或 Agent 任务结束）后，**整个页面卡死无响应**。

---

## 2. 崩溃链路分析

### 2.1 触发流程

```
用户点击停止按钮
    │
    ▼
chatEngine.cancelRequest()
    ├─ m_toolCancelled = true          ← 标记取消
    ├─ m_client->cancelCurrent()       ← 中止 HTTP 请求
    │     ├─ m_reply->abort()
    │     └─ m_reply->deleteLater()
    └─ setLoading(false)               ← ★ 关键：loading 状态变为 false
          │
          └─ emit loadingChanged()
                 │
                 ▼
          pageReadTimer 的 running 条件重新满足
          （条件：selectedHtmlPath !== "" && !webView.loading && !chatEngine.isLoading）
                 │
                 ▼
          pageReadTimer 立即启动（1000ms 间隔）
```

### 2.2 卡死原因链

```
pageReadTimer 每 1000ms 触发
    │
    ├─ webView.runJavaScript("...")          ← ① JS 调用排队进入 Chromium 渲染进程
    │                                            大文档渲染中 → JS 执行延迟
    │
    ├─ 回调: prefetchPage(path, page)         ← ② 启动后台线程
    │   └─ QtConcurrent::run
    │       └─ readHtmlPage(path, page)       ← ③ 内存映射文件 + HTML 解析
    │           └─ pageCache.put()             ← ④ 写锁竞争
    │
    ├─ prefetchPage(path, page+1)             ← ⑤ 又一个后台线程
    │
    └─ 同时: cancelRequest 触发的 m_reply->abort()
            → QNetworkReply::finished 信号    ← ⑥ 网络事件
            → 但 m_reply 已被 deleteLater，lambda 提前 return
            → errorOccurred 不会发射
```

**关键死锁点**：

```
事件循环队列中同时堆积：
┌─────────────────────────────────────────────┐
│  runJavaScript 回调（Chromium → Qt 主线程）   │
│  prefetchPage 线程完成 → invokeMethod 回调    │
│  QNetworkReply::finished 信号                 │
│  QNetworkReply::deleteLater 清理事件          │
│  loadingChanged → QML binding 重新计算        │
│  pageReadTimer 下一次触发                      │
│  WebEngineView 渲染帧回调                      │
└─────────────────────────────────────────────┘
         ↓ 全部在主线程排队处理
         ↓ WebEngineView 的 runJavaScript 回调特别重
         ↓ 形成事件风暴 → 主线程饱和 → UI 卡死
```

### 2.3 根本原因

| 层级 | 问题 | 影响 |
|------|------|------|
| **触发层** | `setLoading(false)` 导致 `pageReadTimer` 立即重启，没有任何冷却期 | Timer 在取消的同时就开始新一轮 JS 调用 |
| **竞争层** | `runJavaScript` 回调、`prefetchPage` 回调、网络事件同时涌入主线程事件循环 | 事件风暴，主线程过载 |
| **资源层** | WebEngineView 渲染大文档时 Chromium 进程已高负载，`runJavaScript` 进一步加剧 | GPU/CPU 资源耗尽 |
| **同步层** | `prefetchPage` 在后台线程调用 `pageCache.put()`，持写锁期间可能和主线程 `pageCache.get()` 产生竞争 | 延迟增加 |

---

## 3. 为什么旧方案没有这个问题

旧方案是 `pagePollTimer`（500ms 轮询）+ `pageContextTimer`（600ms 去抖触发 `loadFilePage`）。

旧方案同样有 `pagePollTimer` 在 cancel 后重启的问题，但当时：
1. `pageContextTimer` 有 600ms 去抖，不是每次轮询都触发 I/O
2. 旧方案没有 `prefetchPage` + `PageTextCache` 的并发写入路径
3. 旧方案的 `loadFilePage` 只做一次 `QtConcurrent::run`，新方案做了两次（当前页 + 下一页）

**新方案在优化了正常路径的同时，意外恶化了取消路径。**

---

## 4. 修复方案

### 4.1 方案 A：取消后增加冷却期（最小改动）

在 `cancelRequest` 后给 `pageReadTimer` 加一个 2-3 秒的冷却期，等取消相关的事件全部处理完再恢复轮询。

```qml
// KnowledgeBasePage.qml 新增
property bool pageReadCoolingDown: false

// 修改 cancel 逻辑
MouseArea {
    onClicked: {
        if (chatEngine.isLoading) {
            chatEngine.cancelRequest()
            pageReadCoolingDown = true
            pageReadCooldownTimer.restart()
        } else {
            kbSendBtn.doSend()
        }
    }
}

Timer {
    id: pageReadCooldownTimer
    interval: 3000   // 3秒冷却期
    repeat: false
    onTriggered: pageReadCoolingDown = false
}

// pageReadTimer 的 running 条件增加冷却检查
Timer {
    id: pageReadTimer
    running: selectedHtmlPath !== "" && !/\.md$/i.test(selectedHtmlPath)
             && !webView.loading && !chatEngine.isLoading
             && !pageReadCoolingDown    // ★ 新增
    // ...
}
```

**优点**：改动极小，立即见效
**缺点**：治标不治本，冷却期内用户翻页不会触发预取

### 4.2 方案 B：彻底消除定时器（推荐，方案文档已规划）

用轻量级 `scroll` 事件监听完全替代 `pageReadTimer` 定时器：

```
scroll 事件 → 300ms 去抖 → 只更新 htmlCurrentPage + 触发 prefetchPage

无定时器 = 无周期轮询 = 无取消后重启问题
```

**优点**：根治，且就是我们方案文档里规划的方向
**缺点**：改动稍大，需要完整实施 ScrollStopDetector

### 4.3 方案 C：降低并发预取（快速缓解）

只预取当前页，不预取下一页，减少一半的并发操作：

```qml
// 当前代码（触发 2 个后台线程）
chatEngine.prefetchPage(selectedHtmlPath, result)
chatEngine.prefetchPage(selectedHtmlPath, result + 1)  // ← 删除这行
```

**优点**：一行改动
**缺点**：部分缓解，不能根治

---

## 5. 建议实施顺序

| 优先级 | 方案 | 说明 |
|--------|------|------|
| **立即** | 方案 A + C 组合 | 加 3s 冷却期 + 只预取当前页，快速止血 |
| **Phase 2** | 方案 B | 按知识库方案文档的 ScrollStopDetector 计划完整实施 |

---

> **分析日期**：2026-07-04
> **相关文档**：[[知识库Agent高效获取Web输入数据方案]]
