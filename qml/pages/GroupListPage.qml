import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    signal openCreate()
    signal openEdit(int groupId)
    signal openChat(int groupId, string groupName)

    background: Rectangle { color: "#FAFAFA" }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0" }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Text {
                text: "Agent 组"
                font.pixelSize: 18; font.bold: true; color: "#212121"
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "+ 新建"
                Material.foreground: "white"
                Material.background: "#1976D2"
                onClicked: root.openCreate()
            }
        }
    }

    ListView {
        id: groupList
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8
        clip: true

        model: ListModel { id: groupModel }

        delegate: Rectangle {
            width: groupList.width
            height: 72
            radius: 10
            color: "white"
            border.color: "#E0E0E0"; border.width: 1

            RowLayout {
                anchors { fill: parent; margins: 14 }
                spacing: 12

                // group avatar
                Rectangle {
                    width: 44; height: 44; radius: 8
                    color: "#7C4DFF"
                    Text {
                        anchors.centerIn: parent
                        text: model.name.charAt(0).toUpperCase()
                        color: "white"; font.bold: true; font.pixelSize: 16
                    }
                }

                // name + member count (clickable area for chat)
                Item {
                    Layout.fillWidth: true; height: parent.height
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openChat(model.groupId, model.name)
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            text: model.name
                            font.pixelSize: 15; font.bold: true; color: "#212121"
                            elide: Text.ElideRight; width: parent.width
                        }
                        Text {
                            text: model.agentCount + " 个成员"
                            font.pixelSize: 12; color: "#757575"
                        }
                    }
                }

                // independent buttons (not inside MouseArea)
                Row {
                    spacing: 4
                    Button {
                        text: "添加"
                        Material.background: "#1976D2"
                        Material.foreground: "white"
                        font.pixelSize: 12
                        implicitHeight: 32
                        onClicked: root.openEdit(model.groupId)
                    }
                    Button {
                        text: "删除"
                        Material.background: "#E53935"
                        Material.foreground: "white"
                        font.pixelSize: 12
                        implicitHeight: 32
                        onClicked: {
                            groupChat.deleteGroup(model.groupId)
                        }
                    }
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: groupList.count === 0
            text: "暂无 Agent 组，点击右上角新建"
            color: "#9E9E9E"; font.pixelSize: 14
        }
    }

    function loadGroups() {
        groupModel.clear()
        const groups = groupChat.getGroups()
        for (const g of groups)
            groupModel.append({
                groupId:    g.id,
                name:       g.name,
                agentCount: g.agentCount,
                createdAt:  g.createdAt
            })
    }

    Component.onCompleted: loadGroups()
    Connections { target: groupChat; function onGroupsChanged() { loadGroups() } }
}
