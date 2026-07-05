Agent Creator
基于 Qt6/QML 的桌面端 AI Agent 管理工具。支持创建可对话的 Agent、本地 PDF 知识库构建、Agent 群组协作和对比分析。

┌──────────────────────────────────────────────────────┐
│  ←  🤖  👥  📚  ⚖️  ⚙️                               │
│  ─────────────────────────────────────────────────── │
│                                                      │
│  ┌──────────┐  ┌──────────────────┐  ┌────────────┐ │
│  │ Agent 列表│  │  HTML 知识库预览  │  │  Agent 对话  │ │
│  │          │  │                  │  │            │ │
│  │ 📄 研报1  │  │  ┌────────────┐  │  │ 用户：...   │ │
│  │ 📄 财报2  │  │  │  第 5 页    │  │  │ Agent：...  │ │
│  │ 📝 笔记   │  │  │  ...内容... │  │  │            │ │
│  │          │  │  └────────────┘  │  │  [输入框]   │ │
│  └──────────┘  └──────────────────┘  └────────────┘ │
│                                                      │
└──────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────┐
│                  QML UI 层                    │
│  Main.qml  →  各页面组件 (Agent / KB / Chat)  │
├─────────────────────────────────────────────┤
│               C++ 业务逻辑层                  │
│  ┌──────────┐ ┌──────────┐ ┌─────────────┐  │
│  │ChatEngine│ │PdfConverter│ │ToolExecutor │  │
│  │ 对话引擎  │ │ PDF→HTML  │ │ 工具执行(6个)│  │
│  └──────────┘ └──────────┘ └─────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌─────────────┐  │
│  │LLMClient │ │PageTextCache│ │AgentManager│  │
│  │ LLM 调用  │ │ 页面缓存   │ │ Agent 管理  │  │
│  └──────────┘ └──────────┘ └─────────────┘  │
├─────────────────────────────────────────────┤
│                 数据层                       │
│  ┌──────────────────┐ ┌──────────────────┐  │
│  │  SQLite (agents, │ │  文件系统 (KB)    │  │
│  │  messages, docs)  │ │  HTML / MD / PDF │  │
│  └──────────────────┘ └──────────────────┘  │
└─────────────────────────────────────────────┘



LLMClient

ChatEngine

ToolExecutor

PdfConverter

PageTextCache

AgentManager





web_search

read_file

write_file

http_request

convert_pdf_to_html

read_html_page










git clone https://github.com/你的用户名/仓库名.git
cd 仓库名

mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x
cmake --build . --config Release






stock_project/
├── main.cpp                  # 入口，注册上下文属性
├── Main.qml                  # 主窗口 + 导航
├── CMakeLists.txt            # 构建配置
├── qml/
│   ├── pages/
│   │   ├── AgentListPage.qml     # Agent 列表
│   │   ├── AgentFormPage.qml     # Agent 创建/编辑表单
│   │   ├── ChatPage.qml          # Agent 对话
│   │   ├── KnowledgeBasePage.qml # 知识库（PDF 预览 + 对话 + 笔记）
│   │   ├── ComparisonPage.qml    # Agent 对比
│   │   ├── GroupListPage.qml     # 群组列表
│   │   ├── GroupFormPage.qml     # 群组创建/编辑
│   │   ├── GroupChatPage.qml     # 群组对话
│   │   └── SettingsPage.qml      # 设置
│   └── components/
│       ├── AgentCard.qml
│       ├── MessageBubble.qml
│       └── TokenIndicator.qml
└── src/
    ├── llm/
    │   └── LLMClient.h/cpp       # LLM HTTP 客户端
    ├── chat/
    │   ├── ChatEngine.h/cpp      # 单 Agent 对话引擎
    │   ├── GroupChatEngine.h/cpp # 群组对话引擎
    │   └── ComparisonEngine.h/cpp# 对比引擎
    ├── database/
    │   ├── DatabaseManager.h/cpp # SQLite 管理
    │   ├── AgentDao.h/cpp        # Agent 数据访问
    │   └── NoteDao.h/cpp         # 笔记数据访问
    ├── services/
    │   ├── AgentManager.h/cpp    # Agent 业务逻辑
    │   ├── PdfConverter.h/cpp    # PDF 转换 + 笔记管理
    │   ├── PageTextCache.h/cpp   # 页面文本 LRU 缓存
    │   ├── TemplateStore.h/cpp   # Agent 模板
    │   └── Tracker.h/cpp         # Token 用量追踪
    ├── tools/
    │   ├── ToolRegistry.h/cpp    # 工具注册表
    │   └── ToolExecutor.h/cpp    # 工具执行器
    └── models/
        ├── AgentData.h           # Agent 数据结构
        ├── MessageData.h         # 消息数据结构
        ├── NoteData.h            # 笔记数据结构
        ├── TemplateData.h        # 模板数据结构
        └── AgentListModel.h/cpp  # QML 列表模型
