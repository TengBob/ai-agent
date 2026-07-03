import QtQuick
import QtQuick.Controls.Material

Rectangle {
    id: root
    property string role: "user"
    property string content: ""
    property string timestamp: ""

    width: parent.width
    height: col.implicitHeight + 16
    color: "transparent"

    readonly property bool isToolMsg: role === "tool_call" || role === "tool_result"
    readonly property bool isUser:    role === "user"
    readonly property bool isAgent:   role.startsWith("agent:")

    // Parse "agent:<id>:<name>" format
    readonly property string agentName: isAgent ? role.split(":")[2] || "" : ""
    readonly property string agentId:   isAgent ? role.split(":")[1] || "" : ""

    // Deterministic color per agent id
    readonly property color agentColor: {
        if (!isAgent) return "#616161"
        const colors = ["#E53935","#8E24AA","#3949AB","#00897B","#43A047",
                        "#F9A825","#6D4C41","#546E7A","#D81B60","#1E88E5"]
        const idx = parseInt(agentId) % colors.length
        return colors[idx]
    }

    Column {
        id: col
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4

        // role label + timestamp
        Row {
            spacing: 8
            Text {
                text: {
                    if (isUser)        return "你"
                    if (isToolMsg && role === "tool_call")   return "⚙ 工具调用"
                    if (isToolMsg && role === "tool_result") return "📋 工具结果"
                    if (isAgent)      return agentName
                    return "AI"
                }
                font.pixelSize: 11; font.bold: true
                color: {
                    if (isUser)                 return "#1976D2"
                    if (role === "tool_call")   return "#F57C00"
                    if (role === "tool_result") return "#388E3C"
                    if (isAgent)               return agentColor
                    return "#616161"
                }
            }
            Text {
                text: timestamp
                font.pixelSize: 11
                color: "#BDBDBD"
                visible: timestamp.length > 0
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // bubble
        Rectangle {
            width: Math.min(bubbleEdit.implicitWidth + 32,
                            root.width * (isToolMsg ? 0.95 : 0.88))
            height: bubbleEdit.implicitHeight + 24
            radius: 10
            color: {
                if (isUser)                 return "#E3F2FD"
                if (role === "tool_call")   return "#FFF8E1"
                if (role === "tool_result") return "#F1F8E9"
                return "white"
            }
            border.color: {
                if (isUser)                 return "#90CAF9"
                if (role === "tool_call")   return "#FFD54F"
                if (role === "tool_result") return "#AED581"
                if (isAgent)               return agentColor
                return "#E0E0E0"
            }
            border.width: isAgent ? 2 : 1

            TextEdit {
                id: bubbleEdit
                anchors {
                    top: parent.top; topMargin: 12
                    left: parent.left; leftMargin: 14
                    right: parent.right; rightMargin: 14
                }

                text: content
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                selectByKeyboard: true

                color: "#212121"
                selectionColor: "#BBDEFB"
                selectedTextColor: "#212121"

                font.pixelSize: isToolMsg ? 12 : 14
                font.family: isToolMsg ? "Courier New" : ""

                textFormat: (!isToolMsg && !isUser) ? TextEdit.MarkdownText : TextEdit.PlainText

                cursorDelegate: Item {}

                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
    }
}
