# 知识库 Agent 高效获取左侧 Web 输入数据 — 优化方案

## 1. 现状分析

### 1.1 当前数据流

```
用户滚动 WebEngineView
    │
    ▼
pagePollTimer（500ms 定时器，持续轮询）
    │  在 WebEngineView 中执行 JS：遍历所有 div.pf，计算 scrollTop + offsetTop
    │  目的：判断用户当前正在看第几页
    │
    ▼
pageContextTimer（600ms 去抖定时器）
    │  触发 chatEngine.loadFilePage(path, page)
    │
    ▼
ChatEngine::loadFilePage → QtConcurrent::run（后台线程）
    │  PdfConverter::readHtmlPage：内存映射文件 → 找页码 div → 剥离 HTML 标签
    │  截断到 4000 字符 → 存入 m_contextContent
    │
    ▼
用户点「发送」
    │  sendMessage() 无条件把 m_contextContent 拼到 prompt 前面
    │  → 发送给 LLM
```

### 1.2 已识别的问题

| # | 问题 | 具体影响 |
|---|------|----------|
| **P1** | `pagePollTimer` 每 500ms 在 WebEngineView 中执行 DOM 遍历 | Chromium 渲染线程频繁被 JS 中断，**大文档滚动时掉帧、卡顿** |
| **P2** | 每次翻页都触发文件 I/O + HTML 标签剥离 | 用户快速浏览几十页（但从未提问），白白消耗 CPU 和磁盘 |
| **P3** | **零缓存**：同一页来回翻 5 次，文件被解析 5 次 | 重复劳动，内存映射 + 正则剥离每次都重新做 |
| **P4** | 上下文**无条件拼接**到 prompt | 用户问"今天天气怎么样"也会把 4000 字的 PDF 内容塞进去 |
| **P5** | 两个 Timer 持续运行，即使用户在看 .md 文件 | 不必要的开销 |

### 1.3 问题根源

**旧模型 = Push 模式**：翻页 → 自动提取文本 → 存储 → 无条件注入

这导致"用户还没问，系统已经干了很多活"。

---

## 2. 优化方案总览

**新模型 = 预取（Prefetch）+ 缓存（Cache）+ 异步兜底（Async Fallback）**

核心思路分三步：

1. **ScrollStopDetector 触发预取**（后台线程，不阻塞 UI）— 用户滚动停止后自动把当前页文本提前提取好
2. **sendMessage 直接查缓存** — 预取已提前完成，几乎 100% 命中，主线程开销 < 0.1ms
3. **未命中走异步兜底** — 极其罕见（用户秒发），异步提取完成后继续发送

```
┌──────────────────────────────────────────────────────────────────┐
│                         QML 层                                    │
│                                                                  │
│  WebEngineView                     Agent Chat Panel              │
│  ┌──────────────────┐              ┌──────────────────────┐      │
│  │ ScrollStopDetector│              │  发送按钮              │      │
│  │ (300ms 去抖)      │              │                      │      │
│  │                   │              │  doSend()            │      │
│  │  ① 更新页码       │              │  ① setCurrentFile... │      │
│  │  ② 触发预取 ──────┼──┐           │  ② sendMessage()    │      │
│  └──────────────────┘  │           └──────────┬───────────┘      │
│                        │                      │                  │
├────────────────────────┼──────────────────────┼──────────────────┤
│                     C++ 层                    │                  │
│                        ▼                      ▼                  │
│  ┌────────────────────────────┐  ┌────────────────────────────┐ │
│  │  prefetchPage(path, page)  │  │  sendMessage(content)       │ │
│  │                            │  │                            │ │
│  │  QtConcurrent::run ───┐    │  │  ① cache.get() ── 查缓存   │ │
│  │     readHtmlPage      │    │  │     │                      │ │
│  │     cache.put()  ◀────┘    │  │     ├─ 命中（99%+）→ 拼接  │ │
│  │                            │  │     │             → 发送    │ │
│  └────────────────────────────┘  │     │                      │ │
│                                  │     └─ 未命中（罕见）       │ │
│  ┌────────────────────────────┐  │         → asyncReadPage    │ │
│  │  PageTextCache (NEW)       │  │         → 缓存 → 拼接→发送 │ │
│  │                            │  │                            │ │
│  │  线程安全 LRU              │◀─┼─── 读/写                   │ │
│  │  容量：10 页               │  │                            │ │
│  │  淘汰：最久未访问          │  └────────────────────────────┘ │
│  └────────────────────────────┘                                │
└──────────────────────────────────────────────────────────────────┘
```

**三个核心原则**：预取不卡 UI（后台线程）、缓存不重复（LRU）、发送不等待（缓存命中 < 0.1ms）

---

## 3. 详细设计

### 3.1 ScrollStopDetector：替换定时器轮询 + 触发预取

**当前代码**（KnowledgeBasePage.qml ~1134-1173 行）：

```qml
// ❌ 问题代码
Timer {
    id: pagePollTimer
    interval: 500; repeat: true       // 每 500ms 执行 JS，持续运行
    running: selectedHtmlPath !== "" && !webView.loading && !chatEngine.isLoading
    onTriggered: {
        webView.runJavaScript("...遍历所有div.pf...")  // DOM 遍历
    }
}
Timer {
    id: pageContextTimer
    interval: 600; repeat: false
    onTriggered: {
        chatEngine.loadFilePage(...)   // ← 翻页就提取文本！
    }
}
```

**改为**：WebView 加载完成后注入 scroll 事件监听。滚动停止 300ms 后执行两个动作：

- ① 更新 `htmlCurrentPage`（轻量，只记页码）
- ② 触发 `chatEngine.prefetchPage(path, page)`（后台线程预取文本）

```qml
// ✅ 新方案：ScrollStopDetector 注入
function installScrollDetector() {
    webView.runJavaScript(`
(function() {
    if (window.__kbScrollInstalled) return;
    window.__kbScrollInstalled = true;
    var timer = null;
    var pc = document.getElementById('page-container');
    if (!pc) return;
    pc.addEventListener('scroll', function() {
        if (timer) clearTimeout(timer);
        timer = setTimeout(function() {
            var pfs = pc.querySelectorAll('div.pf');
            if (pfs.length === 0) return;
            var sy = pc.scrollTop;
            for (var i = 0; i < pfs.length; i++) {
                if (pfs[i].offsetTop + pfs[i].offsetHeight * 0.5 > sy) {
                    var el = document.getElementById('__kb_current_page');
                    if (!el) {
                        el = document.createElement('div');
                        el.id = '__kb_current_page';
                        el.style.display = 'none';
                        document.body.appendChild(el);
                    }
                    el.textContent = (i + 1);
                    break;
                }
            }
        }, 300);  // 滚动停止 300ms 后才触发
    }, {passive: true});  // passive: 不阻塞渲染
})();
    `)
}
```

保留低频轮询读取 `__kb_current_page` 的值（每 2 秒），检测到页码变化后：

```qml
// 页码变化 → 更新 UI + 触发后台预取
Timer {
    id: pagePollFallback
    interval: 2000; repeat: true
    running: selectedHtmlPath !== "" && !webView.loading
    onTriggered: {
        webView.runJavaScript(
            "document.getElementById('__kb_current_page') ? document.getElementById('__kb_current_page').textContent : '0'",
            function(result) {
                var page = parseInt(result) || 0
                if (page > 0 && page !== htmlCurrentPage) {
                    htmlCurrentPage = page
                    htmlCurrentInput = "" + page
                    // ★ 触发预取（后台线程，不阻塞 UI）
                    chatEngine.prefetchPage(selectedHtmlPath, page)
                    // ★ 预取下页（用户在往下读，大概率会翻到）
                    chatEngine.prefetchPage(selectedHtmlPath, page + 1)
                }
            }
        )
    }
}
```

**收益**：
- ❌ 旧：每 500ms 执行一次 DOM 遍历 + 遍历后提取文本
- ✅ 新：每 2000ms 读一个隐藏元素的 textContent（O(1)）；提取在后台线程异步完成
- JS 执行频率降低 **75%**，DOM 操作从"遍历所有 div.pf"降为"读一个元素"

---

### 3.2 PageTextCache：新增线程安全 LRU 缓存

**新文件**：`src/services/PageTextCache.h`

```cpp
#pragma once
#include <QObject>
#include <QHash>
#include <QPair>
#include <QReadWriteLock>
#include <QString>
#include <QDateTime>
#include <optional>

class PageTextCache : public QObject
{
    Q_OBJECT
public:
    explicit PageTextCache(QObject *parent = nullptr);

    // 线程安全：查缓存（O(1)，主线程调用 < 0.1ms）
    std::optional<QString> get(const QString &filePath, int page);

    // 线程安全：写缓存（后台线程调用）
    void put(const QString &filePath, int page, const QString &text);

    // 文件变化（重新转换等）时使相关缓存失效
    void invalidate(const QString &filePath);

    void clear();
    void setMaxEntries(int n);  // 默认 10

private:
    struct Entry { QString text; QDateTime accessTime; };
    void evictIfNeeded();       // LRU 淘汰

    QReadWriteLock m_lock;
    QHash<QPair<QString, int>, Entry> m_cache;
    int m_maxEntries = 10;
};
```

**关键设计决策**：
- `QReadWriteLock` — `get()` 使用读锁，多个线程可并发读；`put()` 使用写锁，独占写入
- Key = `(文件路径, 页码)` — O(1) 查找
- LRU 淘汰 — 超容量时淘汰最久未访问的

---

### 3.3 ChatEngine：预取 + 缓存 + 异步兜底

**这是本次方案的核心。** `sendMessage` 绝不在主线程做文件 I/O。

#### 3.3.1 接口新增

```cpp
// ===== ChatEngine.h 新增 =====
Q_INVOKABLE void setCurrentFileContext(const QString &filePath, int page);
Q_INVOKABLE void prefetchPage(const QString &filePath, int page);

private:
    void sendWithContext(const QString &userContent, const QString &pageText);
    void doSendMessage(const QString &finalContent);
    PageTextCache *m_pageCache;
    QString m_contextFile;
    int    m_contextPage = 0;
    QString m_pendingMessage;  // 异步兜底时暂存用户消息
```

#### 3.3.2 prefetchPage — 后台预取（ScrollStopDetector 触发）

```cpp
// ✅ 在后台线程执行，不阻塞 UI
void ChatEngine::prefetchPage(const QString &filePath, int page)
{
    if (filePath.isEmpty() || page < 1) return;

    // 已有缓存则跳过
    if (m_pageCache->get(filePath, page).has_value()) return;

    // 后台线程提取，完成自动写缓存
    QtConcurrent::run([this, filePath, page]() {
        if (!s_pdfConverter) return;
        QString text = s_pdfConverter->readHtmlPage(filePath, page);
        if (!text.isEmpty()) {
            m_pageCache->put(filePath, page, text);
        }
    });
}
```

#### 3.3.3 sendMessage — 主线程只查缓存

```cpp
void ChatEngine::sendMessage(const QString &content)
{
    if (m_loading || content.trimmed().isEmpty()) return;
    if (m_convId < 0) { emit errorOccurred("未选择对话"); return; }

    // 先保存用户消息
    saveAndEmit("user", content.trimmed());

    QString finalContent = content.trimmed();

    // ── 有上下文时：查缓存 ──
    if (!m_contextFile.isEmpty() && m_contextPage > 0) {

        // ① 查 PageTextCache（主线程，< 0.1ms，纯内存操作）
        auto cached = m_pageCache->get(m_contextFile, m_contextPage);

        if (cached.has_value()) {
            // ═══ 快速路径（99%+ 场景）═══
            // 预取早已完成，直接拼接发送
            finalContent = buildContextualPrompt(cached.value(), finalContent);
            doSendMessage(finalContent);

        } else {
            // ═══ 兜底路径（罕见：用户秒发，预取未完成）═══
            // 异步提取，完成后继续发送
            m_pendingMessage = finalContent;
            setLoading(true);

            QtConcurrent::run([this, path = m_contextFile, page = m_contextPage]() {
                QString text = s_pdfConverter->readHtmlPage(path, page);
                if (!text.isEmpty()) {
                    m_pageCache->put(path, page, text);
                }
                // 回到主线程继续发送
                QMetaObject::invokeMethod(this, [this, text]() {
                    QString final = buildContextualPrompt(text, m_pendingMessage);
                    m_pendingMessage.clear();
                    doSendMessage(final);
                }, Qt::QueuedConnection);
            });
        }

    } else {
        // 无上下文，直接发送
        doSendMessage(finalContent);
    }
}

// 拼接上下文 prompt
QString ChatEngine::buildContextualPrompt(const QString &pageText,
                                           const QString &userContent)
{
    const QString fileName = m_contextFile.split('/').last().split('\\').last();
    return QString(
        "【当前阅读位置】文件：%1，第 %2 页\n\n"
        "%3\n\n"
        "【用户问题】%4"
    ).arg(fileName).arg(m_contextPage).arg(pageText).arg(userContent);
}

// 实际发送给 LLM
void ChatEngine::doSendMessage(const QString &finalContent)
{
    m_context.append(QJsonObject{{"role","user"},{"content",finalContent}});
    m_toolCancelled = false;
    setLoading(true);
    m_loopIter = 0;
    m_dao.insertLog(m_agent.id, m_convId, "chat_start", m_agent.modelName, 0, 0, 0);
    m_client->sendMessage(m_agent.provider, m_agent.modelName,
                          m_agent.apiKey, m_agent.baseUrl,
                          m_context, m_agent.temperature, m_agent.maxTokens,
                          ToolRegistry::instance().toOpenAITools());
}
```

#### 3.3.4 预取为何几乎必命中

```
时间线分析（典型用户行为）：

  t=0ms     用户滚动到第 10 页，停止滚动
  t=300ms   ScrollStopDetector 触发
            → 更新 htmlCurrentPage = 10
            → chatEngine.prefetchPage(path, 10) 启动
            → chatEngine.prefetchPage(path, 11) 启动（预取下一页）
  t=350ms   后台线程完成 readHtmlPage(10) → cache.put()  ← 预取完成！
  t=360ms   后台线程完成 readHtmlPage(11) → cache.put()
  t=500ms   用户开始阅读第 10 页内容
  t=2000ms  用户开始打字提问
  t=5000ms  用户点「发送」
            → cache.get(path, 10) → 命中！→ 直接拼接 → 发送

  预取在 t=350ms 完成，用户 t=5000ms 发送
  → 缓存命中窗口 = 4650ms（4.65 秒的缓冲）
```

**唯一会 miss 的场景**：用户在滚动停止后 **立刻** 点发送（< 300ms），
此时预取刚启动还没完成，走异步兜底路径：
- 暂存消息到 `m_pendingMessage`
- 显示 loading 状态
- 后台提取完成 → 回到主线程拼接 → 发送

#### 3.3.5 主线程耗时对比

| 场景 | 旧方案（同步 readHtmlPage） | 新方案（查缓存） |
|------|---------------------------|-----------------|
| 缓存命中（99%+） | — | **< 0.1ms**（纯内存 Hash 查找） |
| 缓存未命中 | **5-100ms 阻塞主线程** | 异步处理，主线程不被阻塞 |
| 发送按钮体验 | 偶尔卡顿、"粘住" | **始终流畅** |

---

### 3.4 QML 层改动汇总

**KnowledgeBasePage.qml 改动**：

| 移除 | 新增 |
|------|------|
| `pagePollTimer`（500ms 轮询 Timer） | `installScrollDetector()` — 注入 scroll 事件监听 |
| `pageContextTimer`（600ms 去抖 Timer） | `pagePollFallback`（2000ms 低频轮询）— 读页码 + 触发预取 |
| `chatEngine.loadFilePage()` 调用 | `chatEngine.prefetchPage()` 调用 |
| — | `chatEngine.prefetchPage(path, page+1)` — 预取下一页 |

```qml
// kbSendBtn.doSend() — 核心改动
function doSend() {
    const txt = kbInput.text.trim()
    if (txt.length === 0 || chatAgentId < 0) return

    kbChatModel.append({ role: "user", content: txt, ... })

    // ★ 传递当前上下文（路径+页码，不含文本）
    if (selectedHtmlPath !== "" && htmlCurrentPage > 0) {
        chatEngine.setCurrentFileContext(selectedHtmlPath, htmlCurrentPage)
    }

    chatEngine.sendMessage(txt)  // 内部查缓存+拼接，主线程无阻塞
    kbInput.text = ""
}
```

**保留 `loadFilePage` 方法（向后兼容）**，但不再被 ScrollStopDetector 调用。
它仍可用于 Agent 工具 `read_html_page` 的上下文装载。

---

## 4. 改动文件清单

| 操作 | 文件 | 说明 |
|------|------|------|
| **新增** | `src/services/PageTextCache.h` | 页面文本 LRU 缓存头文件 |
| **新增** | `src/services/PageTextCache.cpp` | 缓存实现（get/put/invalidate/evict） |
| **修改** | `src/chat/ChatEngine.h` | 新增 `prefetchPage`、`setCurrentFileContext`、`PageTextCache*`、`m_contextPage`、`m_pendingMessage`；新增 `buildContextualPrompt`、`doSendMessage` 私有方法 |
| **修改** | `src/chat/ChatEngine.cpp` | `sendMessage` → 查缓存+异步兜底（两路径）；新增 `prefetchPage` 实现（后台线程）；新增 `buildContextualPrompt`、`doSendMessage` |
| **修改** | `qml/pages/KnowledgeBasePage.qml` | 移除 `pagePollTimer` / `pageContextTimer`；新增 `installScrollDetector` + `pagePollFallback`（2000ms）；`doSend` 改为传路径；所有 `loadFilePage` 调用改为 `prefetchPage` |
| **修改** | `CMakeLists.txt` | 添加 PageTextCache 源文件 |
| **修改** | `main.cpp` | 创建 PageTextCache 实例，注入 ChatEngine |

---

## 5. 预期收益对比

| 指标 | 当前（旧） | 优化后（新） | 改善幅度 |
|------|-----------|-------------|----------|
| WebEngineView 中 JS 执行频率 | 每 500ms 持续 | 每 2000ms（仅读 textContent） | **↓ 75%+** |
| 翻页触发的文件 I/O | 每次翻页（主线程间接触发） | 仅滚动停止后（后台线程） | **↓ 90%+** |
| 同一页重复解析 | 无上限 | 最多 1 次 | **↓ 100%** |
| sendMessage 主线程阻塞 | 未命中时 5-100ms | **< 0.1ms**（99%+ 场景） | **↓ 99%+** |
| 每次请求额外 Token | ~1000（无条件拼接） | 按需（始终拼接，缓存命中不增加延迟） | 行为不变 |
| 快速滚动 50 页的 CPU 开销 | 50 次 内存映射+解析 | 0-2 次（仅预取当前+下页） | **↓ 96%+** |
| 发送按钮卡顿 | 偶尔 | **无**（主线程不做 I/O） | **消除** |

---

## 6. 实施顺序

| 阶段 | 内容 | 预计改动量 |
|------|------|-----------|
| **Phase 1** | 新增 `PageTextCache` 类 → 编译通过 | 2 个新文件，~120 行 |
| **Phase 2** | 修改 `ChatEngine`：新增 `prefetchPage` + `sendMessage` 双路径（命中/未命中）+ `buildContextualPrompt` + `doSendMessage` | ~80 行改动 |
| **Phase 3** | 修改 `KnowledgeBasePage.qml`：移除定时器 → 新增 `installScrollDetector` → `pagePollFallback` 触发 `prefetchPage` | ~40 行改动 |
| **Phase 4** | 修改 `CMakeLists.txt` + `main.cpp` 注册 PageTextCache | ~10 行 |
| **Phase 5** | 编译 + 端到端测试（打开文档→快速翻页→提问→验证） | 验证 |

---

## 7. 风险与降级

| 风险 | 降级策略 |
|------|----------|
| ScrollStopDetector 在某些 HTML 结构下不触发（没有 `id="page-container"`） | `pagePollFallback` 每 2 秒轮询 `__kb_current_page` 的值，作为兜底 |
| 用户秒发消息（< 300ms），预取未完成 | 走异步兜底路径：暂存消息 → 显示 loading → 提取完成 → 继续发送。用户看到短暂加载后自动发送 |
| PDF 重新转换后缓存过期 | 在 `conversionFinished` 信号中调用 `m_pageCache->invalidate(path)` |
| 后台线程中 `readHtmlPage` 和主线程同时访问同一个 filePath 的 `QFile::map` | `readHtmlPage` 每次调用独立 `QFile`（栈上创建），无共享状态，线程安全 |
| 大文件 `readHtmlPage` 耗时较长（50ms+） | 异步路径不受影响；预取在后台静默完成；最长延迟 = 用户停止滚动后 300ms + 提取耗时 ≈ 350-400ms，远早于用户打字+发送的时间 |
