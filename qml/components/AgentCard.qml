import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root
    property int    agentId: 0
    property string agentName: ""
    property string provider: ""
    property string modelName: ""
    property string systemPrompt: ""

    signal editClicked()
    signal deleteClicked()
    signal chatClicked()

    height: 80
    radius: 10
    color: cardHover.containsMouse ? "#F5F5F5" : "white"
    border.color: "#E0E0E0"
    border.width: 1

    Behavior on color { ColorAnimation { duration: 150 } }

    RowLayout {
        anchors { fill: parent; margins: 14 }
        spacing: 12

        // clickable info area (left side)
        Item {
            Layout.fillWidth: true
            height: parent.height

            MouseArea {
                id: cardHover
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.chatClicked()
            }

            RowLayout {
                anchors.fill: parent
                spacing: 12
                // provider avatar
                Rectangle {
                    width: 44; height: 44
                    radius: 8
                    color: {
                        if (provider === "openai")   return "#00A67E"
                        if (provider === "deepseek") return "#1A5CCB"
                        if (provider === "ollama")   return "#FF6B35"
                        return "#9E9E9E"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: {
                            if (provider === "openai")   return "AI"
                            if (provider === "deepseek") return "DS"
                            if (provider === "ollama")   return "OL"
                            return "?"
                        }
                        color: "white"; font.bold: true; font.pixelSize: 14
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: agentName
                        font.pixelSize: 15; font.bold: true
                        color: "#212121"
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Text {
                        text: modelName + "  ·  " + provider
                        font.pixelSize: 12; color: "#757575"
                        elide: Text.ElideRight; width: parent.width
                    }
                }
            }
        }

        // action buttons (right side — not covered by MouseArea)
        Row {
            spacing: 4
            Button {
                width: 56; height: 32
                text: "编辑"
                flat: false
                font.pixelSize: 12
                Material.foreground: "white"
                Material.background: "#1976D2"
                onClicked: root.editClicked()
            }
            Button {
                width: 56; height: 32
                text: "删除"
                flat: false
                font.pixelSize: 12
                Material.foreground: "white"
                Material.background: "#E53935"
                onClicked: root.deleteClicked()
            }
        }
    }
}
