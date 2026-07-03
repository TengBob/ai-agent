# PDF 无损转本地 HTML 知识库功能实现计划

## 上下文

用户希望把本地 PDF 文件转换为可在本地用浏览器直接打开的 HTML，建立一个本地知识库。之前使用 Markdown/Obsidian 时发现图片加载和显示不够方便，因此要求：
- 转换后的 HTML 尽可能保留原 PDF 的排版、图片、字体。
- 图片和字体采用内嵌方式，避免资源路径问题，方便迁移。
- 在现有 Qt 应用的左侧导航栏增加「知识库」入口。
- 同时向 Agent 暴露 `convert_pdf_to_html` 工具。

## 用户已确认决策

1. **转换引擎**：使用 `pdf2htmlEX`（通过 `QProcess` 调用）。
2. **知识库目录**：允许用户自定义，并通过 `QSettings` 持久化。
3. **预览方式**：应用内嵌预览（使用 Qt WebEngine）。
4. **Agent 工具**：需要注册 `convert_pdf_to_html`。
5. **图片处理**：Base64 内嵌到 HTML，输出单个自包含文件。

## 架构设计

### 1. 数据库层 — 新增 `documents` 表

| 列 | 类型 | 说明 |
|---|---|---|
| id | INTEGER PRIMARY KEY AUTOINCREMENT | 自增 |
| title | TEXT NOT NULL | 文档标题（默认 PDF 文件名） |
| source_path | TEXT | 原始 PDF 绝对路径 |
| output_dir | TEXT | HTML 输出目录 |
| html_path | TEXT | index.html 路径 |
| page_count | INTEGER DEFAULT 0 | 页数 |
| status | TEXT DEFAULT 'pending' | `pending` / `converting` / `done` / `error` |
| error_message | TEXT DEFAULT '' | 失败原因 |
| created_at | TEXT DEFAULT (datetime('now','localtime')) | 创建时间 |

在 `src/database/DatabaseManager.cpp` 的 `createTables()` 中追加该表 DDL。

### 2. 后端 — 新增 `PdfConverter` 类

**`src/services/PdfConverter.h/.cpp`** — 继承 `QObject`，注册为上下文属性 `pdfConverter`。

**属性**
- `bool isConverting` — 是否正在转换（READ + NOTIFY `isConvertingChanged`）
- `QString knowledgeBaseDir` — 知识库根目录（READ/WRITE + NOTIFY，持久化到 `QSettings`）
- `QString pdf2HtmlEXPath` — `pdf2htmlEX` 可执行文件路径（READ/WRITE + NOTIFY，持久化到 `QSettings`）

**Q_INVOKABLE 方法**
- `int convertPdf(const QString &pdfPath)` — 启动转换，返回 document id
- `bool deleteDocument(int docId)`
- `bool openInBrowser(int docId)`
- `QVariantList documents()` — 返回所有文档元数据
- `bool setKnowledgeBaseDir(const QString &dir)`
- `bool setPdf2HtmlEXPath(const QString &path)`
- `bool checkPdf2HtmlEX()` — 检查可执行文件是否可用

**信号**
- `void conversionProgress(int docId, int percent, QString message)`
- `void conversionFinished(int docId, bool success, QString error)`
- `void documentsChanged()`

**内部实现**
- `runPdf2htmlEX(int docId, const QString &pdfPath, const QString &outputDir)`
- 使用 `QProcess` 异步执行：
  ```
  pdf2htmlEX --embed-image 1 --embed-font 1 --embed-css 1 \
             --dest-dir {outputDir} {pdfPath} index.html
  ```
- 通过 `readyReadStandardOutput` 和 `finished` 更新进度与状态。

### 3. QML 页面 — 新增 `KnowledgeBasePage.qml`

**`qml/pages/KnowledgeBasePage.qml`**

- 顶部工具栏：标题「知识库」、搜索框、「添加 PDF」按钮、「设置目录」按钮。
- 文档列表：卡片展示标题、页数、状态、创建时间。
  - 已完成：「预览」「在浏览器打开」「删除」
  - 转换中：进度条 + 状态文本
  - 失败：错误信息 + 「重试」「删除」
- 预览区域：使用 `WebEngineView` 加载生成的 `index.html`。
- 空状态：提示添加 PDF 或配置 pdf2htmlEX 路径。
- 使用 `FileDialog` 选择 PDF，`FolderDialog` 选择知识库目录。

### 4. 导航改动 — `Main.qml`

- 左侧导航栏新增「知识库」按钮，图标 📚，`currentNav = 4`。
- `StackLayout` 新增索引 `8 = KnowledgeBasePage`。
- 返回按钮可见条件增加 `inKnowledgeBase`。
- 新增状态属性：`property bool inKnowledgeBase: false`。

### 5. Agent 工具集成

在 `src/tools/ToolRegistry.cpp` 注册 `convert_pdf_to_html`：
- 参数：`pdf_path`（string，必需）、`output_name`（string，可选）。
- 返回：生成的 `index.html` 绝对路径，或错误信息。

在 `src/tools/ToolExecutor.cpp` 增加执行分支，委托给 `PdfConverter`。

## 文件清单

| 操作 | 文件 |
|---|---|
| 新增 | `src/services/PdfConverter.h` |
| 新增 | `src/services/PdfConverter.cpp` |
| 新增 | `qml/pages/KnowledgeBasePage.qml` |
| 修改 | `src/database/DatabaseManager.cpp` — 新增 `documents` 表 |
| 修改 | `src/tools/ToolRegistry.cpp` — 注册 `convert_pdf_to_html` |
| 修改 | `src/tools/ToolExecutor.cpp` — 实现工具调用 |
| 修改 | `main.cpp` — 注册 `pdfConverter` 上下文属性 |
| 修改 | `CMakeLists.txt` — 添加源文件、QML 文件、WebEngine 模块 |
| 修改 | `Main.qml` — 左侧导航栏 + StackLayout 新页面 |

## 实现顺序

1. `DatabaseManager.cpp` 新增 `documents` 表。
2. 实现 `PdfConverter` 后端（`QProcess` 调用 pdf2htmlEX、状态管理、持久化设置）。
3. `main.cpp` + `CMakeLists.txt` 注册与构建配置（含 Qt WebEngine）。
4. 实现 `KnowledgeBasePage.qml`：列表、添加、设置目录、预览。
5. `Main.qml` 导航集成。
6. 编译验证 + 本地 PDF 转换测试。
7. Agent 工具：`ToolRegistry` + `ToolExecutor` 集成。

## 依赖与风险

### 新增依赖
- `Qt6::WebEngineQuick`（内嵌预览）
- `pdf2htmlEX` 可执行文件（运行时依赖，首次使用检测并提示配置）

### 风险
- Qt WebEngine 会显著增加构建产物体积和打包复杂度；如分发困难，可降级为「外置浏览器打开 + 缩略图列表」。
- 扫描版 PDF（纯图片）转换后 HTML 体积较大，UI 上需给出提示。
- `pdf2htmlEX` 在 Windows 下对中文路径支持一般，传参时使用 `QProcess` 参数列表而非命令字符串。

## 验证方式

1. 编译成功，应用启动无报错。
2. 配置 `pdf2htmlEX` 路径和知识库目录后，选择一个 PDF 转换。
3. 转换完成后，在列表中看到文档，状态为 `done`，页数正确。
4. 点击「预览」，WebEngine 中正确显示 HTML 内容，图片和字体正常。
5. 点击「在浏览器打开」，系统默认浏览器能独立打开生成的 `index.html`。
6. 删除文档后，数据库记录和本地文件夹同步删除。
7. （可选）通过 Agent 调用 `convert_pdf_to_html` 工具，返回正确的 HTML 路径。
