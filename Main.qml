import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import Qt.labs.settings 1.0
import QtWebEngine
import "qml/pages"

ApplicationWindow {
    id: window
    width: 1100; height: 720
    minimumWidth: 800; minimumHeight: 560
    visible: true
    title: "Agent Creator"

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.primary: Material.Blue
    Material.accent: Material.LightBlue

    property bool darkMode: false

    // Qt WebEngine on Windows blocks the main thread while the Chromium render
    // process cold-starts. We force that to happen during application startup
    // (covered by a splash) so it does not freeze the Knowledge Base page later.
    property bool startupReady: false

    // persistent tool settings
    property string tavilyKeySetting: ""
    property string workDirSetting: ""

    Settings {
        property alias tavilyKey:         window.tavilyKeySetting
        property alias workDir:           window.workDirSetting
        property alias kbAgentId:         window.kbChatAgentId
        property alias kbAgentName:       window.kbChatAgentName
        property alias kbPanelVisible:    window.kbChatPanelVisible
        property alias kbHtmlPath:        window.kbHtmlPath
        property alias kbHtmlPage:        window.kbHtmlPage
        property alias kbHtmlPageMapJson: window.kbHtmlPageMapJson
    }

    Component.onCompleted: {
        if (tavilyKeySetting.length > 0) {
            chatEngine.setTavilyKey(tavilyKeySetting)
            groupChat.setTavilyKey(tavilyKeySetting)
        }
        if (workDirSetting.length > 0) {
            chatEngine.setWorkDir(workDirSetting)
            groupChat.setWorkDir(workDirSetting)
        }
    }

    onClosing: {
        if (kbLoader.item) {
            kbChatAgentId      = kbLoader.item.chatAgentId
            kbChatAgentName    = kbLoader.item.chatAgentName
            kbChatPanelVisible = kbLoader.item.chatPanelVisible
        }
    }

    // ---- navigation state ----
    property int  currentNav: 0
    property bool inChat: false
    property bool inForm: false
    property int  formAgentId: -1
    property int  chatAgentId: -1
    property string chatAgentName: ""

    // Group navigation state
    property bool inGroupChat: false
    property bool inGroupForm: false
    property int  groupFormId: -1
    property int  groupChatId: -1
    property string groupChatName: ""

    // Knowledge base navigation state
    property bool   inKnowledgeBase:    false
    property int    kbChatAgentId:      -1
    property string kbChatAgentName:    ""
    property bool   kbChatPanelVisible: false
    property string kbHtmlPath:         ""
    property int    kbHtmlPage:         1
    property string kbHtmlPageMapJson:  "{}"

    // ---- root layout ----
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // left nav rail
        Rectangle {
            Layout.preferredWidth: 72
            Layout.fillHeight: true
            color: darkMode ? "#1E1E2E" : "#1565C0"

            Column {
                anchors { top: parent.top; topMargin: 20; horizontalCenter: parent.horizontalCenter }
                spacing: 8

                // back button (shown in sub-pages)
                NavBtn {
                    visible: inChat || inForm || inGroupChat || inGroupForm || inKnowledgeBase
                    icon: "←"
                    label: "返回"
                    onClicked: {
                        inChat = false; inForm = false
                        inGroupChat = false; inGroupForm = false
                        inKnowledgeBase = false
                    }
                }

                NavBtn {
                    icon: "🤖"
                    label: "Agent"
                    selected: currentNav === 0 && !inChat && !inForm && !inGroupChat && !inGroupForm
                    onClicked: { inChat = false; inForm = false; inGroupChat = false; inGroupForm = false; currentNav = 0 }
                }
                NavBtn {
                    icon: "👥"
                    label: "群组"
                    selected: currentNav === 3 && !inChat && !inForm && !inGroupChat && !inGroupForm && !inKnowledgeBase
                    onClicked: { inChat = false; inForm = false; inGroupChat = false; inGroupForm = false; inKnowledgeBase = false; currentNav = 3 }
                }
                NavBtn {
                    icon: "📚"
                    label: "知识库"
                    selected: currentNav === 4 && !inChat && !inForm && !inGroupChat && !inGroupForm && !inKnowledgeBase
                    onClicked: {
                        console.log("[Main] KB nav clicked, currentNav:", currentNav, "-> 4")
                        inChat = false; inForm = false; inGroupChat = false; inGroupForm = false; inKnowledgeBase = false; currentNav = 4
                    }
                }
                NavBtn {
                    icon: "⚖️"
                    label: "对比"
                    selected: currentNav === 1 && !inChat && !inForm && !inGroupChat && !inGroupForm && !inKnowledgeBase
                    onClicked: { inChat = false; inForm = false; inGroupChat = false; inGroupForm = false; inKnowledgeBase = false; currentNav = 1 }
                }
                NavBtn {
                    icon: "⚙️"
                    label: "设置"
                    selected: currentNav === 2 && !inChat && !inForm && !inGroupChat && !inGroupForm && !inKnowledgeBase
                    onClicked: { inChat = false; inForm = false; inGroupChat = false; inGroupForm = false; inKnowledgeBase = false; currentNav = 2 }
                }
            }
        }

        // right content area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            StackLayout {
                id: stackLayout
                anchors.fill: parent
                visible: currentNav !== 4
                currentIndex: {
                    const idx = inForm ? 3
                              : inChat ? 4
                              : inGroupForm ? 6
                              : inGroupChat ? 7
                              : currentNav === 3 ? 5
                              : currentNav === 4 ? 0   // KB handled by overlay Loader; keep StackLayout on harmless index
                              : currentNav
                    console.log("[Main] StackLayout currentIndex:", idx,
                                "currentNav:", currentNav, "inKnowledgeBase:", inKnowledgeBase)
                    return idx
                }

                // 0 — Agent list
                AgentListPage {
                    onOpenCreate: { formAgentId = -2; formAgentId = -1; inForm = true }
                    onOpenEdit:   (id) => { formAgentId = -2; formAgentId = id; inForm = true }
                    onOpenChat:   (id, name) => {
                        chatAgentId = id; chatAgentName = name; inChat = true
                    }
                }

                // 1 — Comparison
                ComparisonPage {}

                // 2 — Settings
                SettingsPage {
                    onThemeToggled: (d) => { window.darkMode = d }
                }

                // 3 — Agent form
                AgentFormPage {
                    editAgentId: window.formAgentId
                    onSaved:     { inForm = false }
                    onCancelled: { inForm = false }
                }

                // 4 — Agent chat
                ChatPage {
                    agentId:   window.chatAgentId
                    agentName: window.chatAgentName
                    onGoBack:  { inChat = false }
                }

                // 5 — Group list
                GroupListPage {
                    onOpenCreate: { groupFormId = -1; inGroupForm = true }
                    onOpenEdit:   (gid) => { groupFormId = -2; groupFormId = gid; inGroupForm = true }
                    onOpenChat:   (gid, gname) => {
                        groupChatId = -1; groupChatName = ""
                        groupChatId = gid; groupChatName = gname; inGroupChat = true
                    }
                }

                // 6 — Group form
                GroupFormPage {
                    editGroupId: window.groupFormId
                    onSaved:     { inGroupForm = false }
                    onCancelled: { inGroupForm = false }
                }

                // 7 — Group chat
                GroupChatPage {
                    groupId:   window.groupChatId
                    groupName: window.groupChatName
                    onGoBack:  { inGroupChat = false }
                }
            }

            // Knowledge base overlay (Loader: fully unload WebEngineView when not active)
            Loader {
                id: kbLoader
                anchors.fill: parent
                active: currentNav === 4
                visible: active
                sourceComponent: active ? kbComponent : null
                onStatusChanged: {
                    console.log("[Main] kbLoader status:", status, "active:", active, "item:", item)
                    if (status === Loader.Error) {
                        console.error("[Main] kbLoader failed to load:", sourceComponent)
                    }
                }
                onActiveChanged: {
                    console.log("[Main] kbLoader active changed:", active)
                    if (!active && item) {
                        window.kbChatAgentId      = item.chatAgentId
                        window.kbChatAgentName    = item.chatAgentName
                        window.kbChatPanelVisible = item.chatPanelVisible
                        window.kbHtmlPath         = item.selectedHtmlPath
                        window.kbHtmlPage         = item.htmlCurrentPage
                        window.kbHtmlPageMapJson  = JSON.stringify(item.htmlPageMap)
                    }
                }
                onLoaded: {
                    console.time("[Main] kbLoader.onLoaded")
                    console.log("[Main] kbLoader item size:", item ? (item.width + "x" + item.height) : "null")
                    const map = window.kbHtmlPageMapJson ? JSON.parse(window.kbHtmlPageMapJson) : {}
                    const restoredPage = Math.max(1, map[window.kbHtmlPath] || window.kbHtmlPage || 1)
                    item.chatAgentId      = window.kbChatAgentId
                    item.chatAgentName    = window.kbChatAgentName
                    item.chatPanelVisible = window.kbChatPanelVisible
                    item.selectedHtmlPath = window.kbHtmlPath
                    item.htmlCurrentPage  = restoredPage
                    item.htmlCurrentInput = "" + restoredPage
                    item.savedHtmlPage    = restoredPage
                    item.htmlPageMap      = map
                    if (window.kbChatAgentId > 0)
                        item.restoreChat(window.kbChatAgentId)
                    if (window.kbHtmlPath !== "") {
                        item.loadHtmlFile(window.kbHtmlPath, restoredPage)
                    }
                    console.timeEnd("[Main] kbLoader.onLoaded")
                    Qt.callLater(function() {
                        console.log("[Main] kbLoader item size delayed:", item ? (item.width + "x" + item.height) : "null")
                    })
                    item.goBack.connect(function() {
                        window.kbChatAgentId      = item.chatAgentId
                        window.kbChatAgentName    = item.chatAgentName
                        window.kbChatPanelVisible = item.chatPanelVisible
                        window.kbHtmlPath         = item.selectedHtmlPath
                        window.kbHtmlPage         = item.htmlCurrentPage
                        window.kbHtmlPageMapJson  = JSON.stringify(item.htmlPageMap)
                        inKnowledgeBase = false
                    })
                    item.openChat.connect(function(agentId, agentName) {
                        chatAgentId   = agentId
                        chatAgentName = agentName
                        inKnowledgeBase = false
                        inChat = true
                    })
                }
            }
            Component {
                id: kbComponent
                KnowledgeBasePage {
                    anchors.fill: parent
                }
            }
        }
    }

    // nav button component
    component NavBtn: Rectangle {
        id: nb
        property string icon: ""
        property string label: ""
        property bool selected: false
        signal clicked()

        width: 60; height: 60; radius: 12
        color: selected
               ? (window.darkMode ? "#3A3A5C" : "#1976D2")
               : (ma.containsMouse ? (window.darkMode ? "#2A2A4C" : "#1565C0") : "transparent")
        Behavior on color { ColorAnimation { duration: 150 } }

        Column {
            anchors.centerIn: parent; spacing: 2
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: nb.icon; font.pixelSize: 20 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: nb.label; font.pixelSize: 10; color: "white" }
        }

        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: nb.clicked() }
    }

    // Warm-up WebEngineView: Qt WebEngine on Windows blocks the main thread while
    // the Chromium render process cold-starts. We force that to happen during
    // application startup, covered by a splash screen, so opening the Knowledge Base
    // page later does not freeze the UI.
    WebEngineView {
        id: webEngineWarmup
        anchors.fill: parent
        visible: !window.startupReady
        url: "about:blank"
        z: 999
        onLoadingChanged: function(req) {
            if (req.status === WebEngineView.LoadSucceededStatus) {
                console.log("[Main] WebEngine warm-up finished")
                window.startupReady = true
            } else if (req.status === WebEngineView.LoadFailedStatus) {
                console.warn("[Main] WebEngine warm-up failed:", req.errorString)
                window.startupReady = true
            }
        }
    }

    Rectangle {
        id: startupSplash
        anchors.fill: parent
        color: window.darkMode ? "#121212" : "white"
        visible: !window.startupReady
        z: 1000

        Column {
            anchors.centerIn: parent
            spacing: 16
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: true }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "正在初始化预览组件..."
                font.pixelSize: 14
                color: window.darkMode ? "#E0E0E0" : "#616161"
            }
        }

        Timer {
            interval: 10000
            running: !window.startupReady
            onTriggered: {
                console.warn("[Main] WebEngine warm-up timeout, dismissing splash")
                window.startupReady = true
            }
        }
    }

    // global error handler from agentManager
    Connections {
        target: agentManager
        function onAgentError(msg) { globalSnack.show(msg) }
    }

    // snackbar
    Rectangle {
        id: globalSnack
        property string message: ""
        function show(msg) { message = msg; opacity = 1; hideTimer.restart() }

        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 24 }
        width: snackText.implicitWidth + 40; height: 40
        radius: 20; color: "#323232"; opacity: 0
        Behavior on opacity { NumberAnimation { duration: 250 } }

        Timer { id: hideTimer; interval: 3000; onTriggered: globalSnack.opacity = 0 }
        Text { id: snackText; anchors.centerIn: parent; text: globalSnack.message; color: "white"; font.pixelSize: 13 }
    }
}
