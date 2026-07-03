import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    property int editGroupId: -1
    readonly property bool isEditMode: editGroupId > 0

    signal saved()
    signal cancelled()

    background: Rectangle { color: "#FAFAFA" }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0" }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Text {
                text: isEditMode ? "管理成员" : "新建 Agent 组"
                font.pixelSize: 18; font.bold: true; color: "#212121"
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "取消"; flat: true
                onClicked: root.cancelled()
            }
            Button {
                text: "保存"
                Material.background: "#1976D2"
                Material.foreground: "white"
                enabled: nameField.text.trim().length > 0
                onClicked: {
                    const name = nameField.text.trim()
                    const ids = getSelectedIds()
                    if (isEditMode) {
                        groupChat.updateGroup(editGroupId, name)
                        groupChat.setGroupAgents(editGroupId, ids)
                    } else {
                        const gid = groupChat.createGroup(name)
                        if (gid > 0) groupChat.setGroupAgents(gid, ids)
                    }
                    root.saved()
                }
            }
        }
    }

    ScrollView {
        anchors.fill: parent; contentWidth: availableWidth

        Column {
            width: Math.min(parent.width - 48, 600)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 16; topPadding: 24; bottomPadding: 32

            // Group name
            Label { text: "组名"; font.pixelSize: 14; color: "#616161" }
            TextField {
                id: nameField
                width: parent.width
                placeholderText: "输入组名称"
                font.pixelSize: 14
            }

            // Agent selection
            Label { text: "选择成员（打勾添加到组）"; font.pixelSize: 14; color: "#616161" }

            Column {
                width: parent.width
                spacing: 0

                Repeater {
                    model: ListModel { id: agentCheckModel }

                    delegate: CheckBox {
                        property int agentId: model.agentId
                        property bool isChecked: model.checked
                        width: parent.width
                        checked: model.checked
                        font.pixelSize: 14
                        text: model.agentName + "  (" + model.agentProvider + " / " + model.agentModel + ")"
                        onCheckedChanged: agentCheckModel.setProperty(index, "checked", checked)
                    }
                }
            }
        }
    }

    function getSelectedIds() {
        const ids = []
        for (let i = 0; i < agentCheckModel.count; i++) {
            if (agentCheckModel.get(i).checked) ids.push(agentCheckModel.get(i).agentId)
        }
        return ids
    }

    function loadForm() {
        agentCheckModel.clear()
        nameField.text = ""

        // Collect already-selected agent IDs for this group
        const selectedIds = []
        if (isEditMode) {
            const groups = groupChat.getGroups()
            for (const g of groups) {
                if (g.id === editGroupId) {
                    nameField.text = g.name
                    break
                }
            }
            const ga = groupChat.getGroupAgents(editGroupId)
            for (const a of ga) selectedIds.push(a.id)
        }

        // Load all agents with checkboxes
        const agents = groupChat.getAllAgents()
        for (const a of agents) {
            agentCheckModel.append({
                agentId:      a.id,
                agentName:    a.name,
                agentProvider: a.provider,
                agentModel:   a.modelName,
                checked:      selectedIds.indexOf(a.id) >= 0
            })
        }
    }

    onEditGroupIdChanged: Qt.callLater(loadForm)
    Component.onCompleted: Qt.callLater(loadForm)
}
