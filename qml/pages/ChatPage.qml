import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Page {
    id: root
    property int    agentId:   -1
    property string agentName: ""
    property bool   logPanelOpen: false
    property bool   logAutoScroll: true

    signal goBack()

    background: Rectangle { color: "#FAFAFA" }

    onAgentIdChanged: {
        if (agentId >= 0) {
            msgModel.clear()
            logModel.clear()
            chatEngine.startConversation(agentId)
            loadCurrentMessages()
            refreshConversations()
        }
    }

    Component.onCompleted: {
        chatEngine.messageReceived.connect(onMessageReceived)
        chatEngine.errorOccurred.connect(onError)
        if (agentId >= 0) {
            chatEngine.startConversation(agentId)
            loadCurrentMessages()
            refreshConversations()
        }
    }

    Component.onDestruction: {
        chatEngine.messageReceived.disconnect(onMessageReceived)
        chatEngine.errorOccurred.disconnect(onError)
    }

    function loadCurrentMessages() {
        msgModel.clear()
        logModel.clear()
        const msgs = chatEngine.currentMessages()
        for (const m of msgs) {
            if (m.role === "tool_call" || m.role === "tool_result")
                logModel.append(m)
            else
                msgModel.append(m)
        }
        if (logModel.count > 0) logPanelOpen = true
        Qt.callLater(() => listView.positionViewAtEnd())
    }

    function onMessageReceived(role, content, ts) {
        if (role === "tool_call" || role === "tool_result") {
            logModel.append({ role: role, content: content, timestamp: ts })
            logPanelOpen = true
            if (logAutoScroll)
                Qt.callLater(() => logListView.positionViewAtEnd())
        } else {
            msgModel.append({ role: role, content: content, timestamp: ts })
        }
        listView.positionViewAtEnd()
    }

    function onError(msg) {
        errorBar.text = msg
        errorBar.visible = true
        errorTimer.restart()
    }

    function refreshConversations() {
        convModel.clear()
        const convs = chatEngine.getConversations(agentId)
        for (const c of convs) convModel.append(c)
        if (convModel.count > 0) convCombo.currentIndex = 0
    }

    function doSend() {
        const txt = inputArea.text.trim()
        if (txt.length === 0) return
        inputArea.text = ""
        chatEngine.sendMessage(txt)
    }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0"; border.width: 1 }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Button { text: "← 返回"; flat: true; onClicked: root.goBack() }
            Text { text: agentName; font.pixelSize: 16; font.bold: true; color: "#212121" }
            Item { Layout.fillWidth: true }
            TokenIndicator {
                used:  chatEngine.totalTokensIn + chatEngine.totalTokensOut
                total: 8192
                compact: true
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── main chat area ──
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true; height: 40
                color: "white"; border.color: "#E0E0E0"; border.width: 1

                RowLayout {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    Text {
                        text: "对话  " + (convModel.count > 0 ? convModel.get(0).title : "")
                        font.pixelSize: 12; color: "#757575"
                        Layout.fillWidth: true
                    }
                    Button {
                        text: "+ 新对话"; flat: true; height: 32; font.pixelSize: 12
                        onClicked: {
                            chatEngine.newConversation()
                            loadCurrentMessages()
                            refreshConversations()
                        }
                    }
                }
            }

            ListView {
                id: listView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true; spacing: 4
                model: ListModel { id: msgModel }

                delegate: MessageBubble {
                    width: listView.width - 8; x: 4
                    role: model.role; content: model.content; timestamp: model.timestamp || ""
                }

                Item {
                    visible: chatEngine.isLoading
                    anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                    width: 120; height: 40
                    BusyIndicator { anchors.left: parent.left; width: 30; height: 30; running: true }
                    Text { anchors.centerIn: parent; text: "AI 思考中..."; font.pixelSize: 12; color: "#9E9E9E" }
                }
            }

            Rectangle {
                id: errorBar
                property string text: ""
                visible: false
                Layout.fillWidth: true; height: 36
                color: "#FFEBEE"; border.color: "#F44336"; border.width: 1
                Text { anchors.centerIn: parent; text: "⚠ " + errorBar.text; color: "#C62828"; font.pixelSize: 13 }
                Timer { id: errorTimer; interval: 4000; onTriggered: errorBar.visible = false }
            }

            Rectangle {
                Layout.fillWidth: true
                height: inputRow.implicitHeight + 16
                color: "white"; border.color: "#E0E0E0"; border.width: 1

                RowLayout {
                    id: inputRow
                    anchors { fill: parent; margins: 8 }
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        height: Math.min(160, Math.max(80, inputArea.contentHeight + 20))
                        radius: 8; color: "#F5F5F5"
                        border.color: inputArea.activeFocus ? "#1976D2" : "#E0E0E0"
                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        ScrollView {
                            anchors { fill: parent; margins: 4 }
                            TextArea {
                                id: inputArea
                                wrapMode: TextArea.Wrap; font.pixelSize: 14; background: Item {}
                                Keys.onPressed: (event) => {
                                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                        if (event.modifiers & Qt.ShiftModifier) { }
                                        else { doSend(); event.accepted = true }
                                    }
                                }
                            }
                        }
                    }

                    Button {
                        text: "发送"
                        enabled: !chatEngine.isLoading && inputArea.length > 0
                        Material.foreground: "white"
                        Material.background: enabled ? "#1976D2" : "#BDBDBD"
                        onClicked: doSend()
                    }
                }
            }
        }

        // ── right-side collapsible log panel (dark theme) ──
        Rectangle {
            id: logPanel
            Layout.fillHeight: true
            Layout.preferredWidth: logPanelOpen ? Math.min(380, root.width * 0.35) : 0
            clip: true
            color: "#1E1E1E"
            border.color: logPanelOpen ? "#333333" : "transparent"; border.width: 1

            Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: logPanelOpen

                // header
                Rectangle {
                    Layout.fillWidth: true; height: 40
                    color: "#2D2D2D"

                    RowLayout {
                        anchors { fill: parent; leftMargin: 12; rightMargin: 8 }

                        Text {
                            text: "工具日志"
                            font.pixelSize: 13; font.bold: true; color: "#E0E0E0"
                        }

                        Rectangle {
                            width: logCountText.implicitWidth + 12; height: 20
                            radius: 10; color: logModel.count > 0 ? "#1A3A5C" : "#333333"
                            Text {
                                id: logCountText
                                anchors.centerIn: parent
                                text: logModel.count
                                font.pixelSize: 11; font.bold: true
                                color: logModel.count > 0 ? "#64B5F6" : "#666666"
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "✕"; flat: true; font.pixelSize: 14
                            implicitWidth: 32; implicitHeight: 32
                            Material.foreground: "#BDBDBD"
                            onClicked: logPanelOpen = false
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#333333" }

                // log list
                ListView {
                    id: logListView
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 4
                    model: ListModel { id: logModel }
                    leftMargin: 6; rightMargin: 6; topMargin: 4; bottomMargin: 4

                    onContentYChanged: {
                        if (moving) {
                            const atBottom = contentY + height >= contentHeight - 20
                            logAutoScroll = atBottom
                        }
                    }

                    delegate: Rectangle {
                        width: logListView.width - 12; x: 6
                        height: logText.implicitHeight + 24
                        radius: 6
                        color: model.role === "tool_call" ? "#3A3520" : "#1E3A2A"
                        border.color: model.role === "tool_call" ? "#5C4A00" : "#2E5C3A"
                        border.width: 1

                        MouseArea {
                            anchors.fill: parent
                            onClicked: { logAutoScroll = false }
                        }

                        Row {
                            anchors { left: parent.left; leftMargin: 8; top: parent.top; topMargin: 4 }
                            spacing: 4
                            Text {
                                text: model.role === "tool_call" ? "📤 调用" : "📥 结果"
                                font.pixelSize: 10; font.bold: true
                                color: model.role === "tool_call" ? "#FFB74D" : "#81C784"
                            }
                            Text {
                                text: model.timestamp || ""
                                font.pixelSize: 9; color: "#666666"
                            }
                        }

                        TextEdit {
                            id: logText
                            anchors { fill: parent; margins: 8; topMargin: 22 }
                            text: model.content
                            wrapMode: TextEdit.Wrap
                            readOnly: true; selectByMouse: true
                            font.pixelSize: 11; font.family: "Courier New"
                            color: "#CCCCCC"
                            selectionColor: "#1976D2"
                            selectedTextColor: "#FFFFFF"
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: logListView.count === 0
                        text: "暂无工具调用"; color: "#555555"; font.pixelSize: 13
                    }
                }
            }
        }
    }

    // toggle button on the right edge
    Rectangle {
        id: logToggle
        visible: !logPanelOpen
        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 0 }
        width: 28; height: 80
        radius: 0
        color: "#37474F"
        opacity: 0.85

        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: 4; color: logModel.count > 0 ? "#1976D2" : "transparent"
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: logToggle.opacity = 1.0
            onExited: logToggle.opacity = 0.85
            onClicked: logPanelOpen = true
        }

        Column {
            anchors.centerIn: parent
            spacing: 4
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "🔧"
                font.pixelSize: 12
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: logModel.count > 0 ? logModel.count : ""
                font.pixelSize: 10; color: "white"; font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "日"
                font.pixelSize: 9; color: "#B0BEC5"
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "志"
                font.pixelSize: 9; color: "#B0BEC5"
            }
        }
    }

    ListModel { id: convModel }
}
