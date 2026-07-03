import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Page {
    id: root
    signal openChat(int agentId, string agentName)
    signal openEdit(int agentId)
    signal openCreate()

    background: Rectangle { color: "#FAFAFA" }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0"; border.width: 1 }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Text { text: "Agent 列表"; font.pixelSize: 18; font.bold: true; color: "#212121" }
            Item { Layout.fillWidth: true }
            TextField {
                id: searchField
                placeholderText: "搜索 Agent..."
                width: 200; height: 36
                font.pixelSize: 13
                background: Rectangle { radius: 6; color: "#F5F5F5"; border.color: "#E0E0E0" }
                onTextChanged: agentManager.searchAgents(text)
            }
            Button {
                text: "+ 新建"
                Material.foreground: "white"
                Material.background: "#1976D2"
                onClicked: root.openCreate()
            }
        }
    }

    ListView {
        id: listView
        anchors { fill: parent; margins: 16 }
        spacing: 8
        model: agentManager.agentListModel
        clip: true

        delegate: AgentCard {
            width: listView.width
            agentId:      model.agentId
            agentName:    model.agentName
            provider:     model.agentProvider
            modelName:    model.agentModelName
            systemPrompt: model.agentSystemPrompt
            onChatClicked:   root.openChat(model.agentId, model.agentName)
            onEditClicked:   root.openEdit(model.agentId)
            onDeleteClicked: deleteDialog.open(model.agentId, model.agentName)
        }

        // empty state
        Text {
            anchors.centerIn: parent
            visible: listView.count === 0
            text: "还没有 Agent\n点击「+ 新建」创建第一个"
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 15; color: "#9E9E9E"; lineHeight: 1.6
        }
    }

    // delete confirm dialog
    Dialog {
        id: deleteDialog
        property int  pendingId: -1
        property string pendingName: ""

        function open(id, name) { pendingId = id; pendingName = name; visible = true }

        title: "删除确认"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        Text {
            text: "确定删除 Agent「" + deleteDialog.pendingName + "」？\n此操作将删除所有对话历史，不可恢复。"
            wrapMode: Text.Wrap; lineHeight: 1.5
        }
        onAccepted: agentManager.deleteAgent(pendingId)
    }
}
