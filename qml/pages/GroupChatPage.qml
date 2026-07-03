import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Page {
    id: root
    property int    groupId:   -1
    property string groupName: ""
    property bool   logPanelOpen: false
    property bool   logAutoScroll: true

    signal goBack()

    background: Rectangle { color: "#FAFAFA" }

    onGroupIdChanged: {
        if (groupId >= 0) {
            msgModel.clear()
            logModel.clear()
            groupChat.startGroupConversation(groupId)
            loadCurrentMessages()
            loadAgentChips()
        }
    }

    Component.onCompleted: {
        groupChat.messageReceived.connect(onMessageReceived)
        groupChat.errorOccurred.connect(onError)
        groupChat.groupsChanged.connect(onGroupsChanged)
        if (groupId >= 0) {
            groupChat.startGroupConversation(groupId)
            loadCurrentMessages()
            loadAgentChips()
        }
    }

    Component.onDestruction: {
        groupChat.messageReceived.disconnect(onMessageReceived)
        groupChat.errorOccurred.disconnect(onError)
        groupChat.groupsChanged.disconnect(onGroupsChanged)
    }

    function onGroupsChanged() {
        if (groupId >= 0) {
            loadAgentChips()
            groupChat.startGroupConversation(groupId)
            loadCurrentMessages()
        }
    }

    onVisibleChanged: {
        if (visible && groupId >= 0) {
            groupChat.startGroupConversation(groupId)
            loadCurrentMessages()
            loadAgentChips()
        }
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

    function loadCurrentMessages() {
        msgModel.clear()
        logModel.clear()
        const msgs = groupChat.groupMessages()
        for (const m of msgs) {
            if (m.role === "tool_call" || m.role === "tool_result")
                logModel.append(m)
            else
                msgModel.append(m)
        }
        if (logModel.count > 0) logPanelOpen = true
        Qt.callLater(() => listView.positionViewAtEnd())
    }

    ListModel { id: agentChipModel }

    function loadAgentChips() {
        agentChipModel.clear()
        const agents = groupChat.getGroupAgents(groupId)
        for (const a of agents)
            agentChipModel.append({ agentId: a.id, agentName: a.name })
    }

    function doSend() {
        const txt = inputArea.text.trim()
        if (txt.length === 0) return
        inputArea.text = ""
        groupChat.sendGroupMessage(txt)
    }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0"; border.width: 1 }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Button { text: "← 返回"; flat: true; onClicked: root.goBack() }
            Text {
                text: groupName
                font.pixelSize: 16; font.bold: true; color: "#212121"
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "+ 新对话"; flat: true; font.pixelSize: 12
                onClicked: {
                    groupChat.newGroupConversation()
                    loadCurrentMessages()
                }
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

            // Members bar
            Rectangle {
                Layout.fillWidth: true
                height: 36
                color: "#F5F5F5"
                border.color: "#E0E0E0"; border.width: 1

                Row {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    spacing: 8
                    clip: true

                    Text {
                        text: "成员："
                        font.pixelSize: 12; color: "#9E9E9E"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Repeater {
                        model: agentChipModel
                        Text {
                            text: "@" + model.agentName
                            font.pixelSize: 12; color: "#1976D2"; font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }

            // message list
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
                    visible: groupChat.isLoading
                    anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                    width: 160; height: 40
                    BusyIndicator { anchors.left: parent.left; width: 30; height: 30; running: true }
                    Text {
                        anchors.centerIn: parent
                        text: "AI 思考中..."
                        font.pixelSize: 12; color: "#9E9E9E"
                    }
                }
            }

            // error bar
            Rectangle {
                id: errorBar
                property string text: ""
                visible: false
                Layout.fillWidth: true; height: 36
                color: "#FFEBEE"; border.color: "#F44336"; border.width: 1
                Text { anchors.centerIn: parent; text: "⚠ " + errorBar.text; color: "#C62828"; font.pixelSize: 13 }
                Timer { id: errorTimer; interval: 4000; onTriggered: errorBar.visible = false }
            }

            // input area
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Text {
                    Layout.leftMargin: 16
                    visible: inputArea.length === 0 && !inputArea.activeFocus
                    text: "💡 输入 @Agent名 分配任务，例如：@助手 搜索新闻 @分析 综合结果"
                    font.pixelSize: 11; color: "#BDBDBD"
                }

                Rectangle {
                    id: inputContainer
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
                                    placeholderText: ""

                                    onTextChanged: {
                                        const cursorPos = cursorPosition
                                        const txt = text
                                        let atPos = -1
                                        for (let i = cursorPos - 1; i >= 0; i--) {
                                            if (txt[i] === '@') { atPos = i; break }
                                            if (txt[i] === ' ' || txt[i] === '\n') break
                                        }

                                        if (atPos >= 0) {
                                            const partial = txt.substring(atPos + 1, cursorPos).toLowerCase()
                                            mentionModel.clear()
                                            for (let j = 0; j < agentChipModel.count; j++) {
                                                const n = agentChipModel.get(j).agentName
                                                if (partial.length === 0 || n.toLowerCase().indexOf(partial) >= 0)
                                                    mentionModel.append({name: n, agentId: agentChipModel.get(j).agentId})
                                            }
                                            if (mentionModel.count > 0) {
                                                mentionPopup.open()
                                            } else {
                                                mentionPopup.close()
                                            }
                                        } else {
                                            mentionPopup.close()
                                        }
                                    }

                                    function insertMention(name) {
                                        const cursorPos = cursorPosition
                                        const txt = text
                                        let atPos = -1
                                        for (let i = cursorPos - 1; i >= 0; i--) {
                                            if (txt[i] === '@') { atPos = i; break }
                                            if (txt[i] === ' ' || txt[i] === '\n') break
                                        }
                                        if (atPos >= 0) {
                                            const before = txt.substring(0, atPos)
                                            const after = txt.substring(cursorPos)
                                            text = before + "@" + name + " " + after
                                            cursorPosition = before.length + name.length + 2
                                        }
                                    }

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
                            id: sendBtn
                            text: "发送"
                            enabled: !groupChat.isLoading && inputArea.length > 0
                            Material.foreground: "white"
                            Material.background: enabled ? "#1976D2" : "#BDBDBD"
                            onClicked: doSend()
                        }
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

    // toggle button on the right edge (visible when panel is closed)
    Rectangle {
        id: logToggle
        visible: !logPanelOpen
        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 0 }
        width: 28; height: 80
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

    // @mention popup
    Popup {
        id: mentionPopup
        parent: Overlay.overlay
        x: root.x + 12
        y: root.height - inputContainer.height - height - 8
        width: 220
        padding: 4

        background: Rectangle {
            radius: 8; color: "white"
            border.color: "#E0E0E0"; border.width: 1
        }

        contentItem: ListView {
            id: mentionList
            clip: true
            spacing: 2
            implicitHeight: Math.min(contentHeight, 200)

            model: ListModel { id: mentionModel }

            delegate: ItemDelegate {
                width: mentionList.width
                height: 40

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    spacing: 8

                    Rectangle {
                        width: 26; height: 26; radius: 4
                        color: "#1976D2"
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            anchors.centerIn: parent
                            text: model.name.charAt(0)
                            color: "white"; font.pixelSize: 11; font.bold: true
                        }
                    }
                    Text {
                        text: model.name
                        font.pixelSize: 13; color: "#212121"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                onClicked: {
                    inputArea.insertMention(model.name)
                    mentionPopup.close()
                }
            }
        }
    }
}
