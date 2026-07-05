cmake_minimum_required(VERSION 3.16)

project(stock_project VERSION 0.1 LANGUAGES CXX)

if(POLICY QTP0004)
    cmake_policy(SET QTP0004 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS
    Quick
    QuickControls2
    Sql
    Network
    WebEngineQuick
    Concurrent
    WebChannel
)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appstock_project
    main.cpp
    src/models/AgentListModel.cpp
    src/database/DatabaseManager.cpp
    src/database/AgentDao.cpp
    src/database/NoteDao.cpp
    src/llm/LLMClient.cpp
    src/chat/ChatEngine.cpp
    src/chat/GroupChatEngine.cpp
    src/chat/ComparisonEngine.cpp
    src/services/AgentManager.cpp
    src/services/TemplateStore.cpp
    src/services/Tracker.cpp
    src/services/PdfConverter.cpp
    src/services/PageTextCache.cpp
    src/tools/ToolRegistry.cpp
    src/tools/ToolExecutor.cpp
)

qt_add_qml_module(appstock_project
    URI stock_project
    VERSION 1.0
    QML_FILES
        Main.qml
        qml/pages/AgentListPage.qml
        qml/pages/AgentFormPage.qml
        qml/pages/ChatPage.qml
        qml/pages/ComparisonPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/GroupListPage.qml
        qml/pages/GroupFormPage.qml
        qml/pages/GroupChatPage.qml
        qml/pages/KnowledgeBasePage.qml
        qml/components/MessageBubble.qml
        qml/components/AgentCard.qml
        qml/components/TokenIndicator.qml
        RESOURCES
)

target_include_directories(appstock_project PRIVATE
    src
    src/models
    src/database
    src/llm
    src/chat
    src/services
)

set_target_properties(appstock_project PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appstock_project
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Sql
        Qt6::Network
        Qt6::WebEngineQuick
        Qt6::Concurrent
        Qt6::WebChannel
)

install(TARGETS appstock_project
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
