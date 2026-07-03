import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    property bool darkMode: false
    signal themeToggled(bool dark)

    background: Rectangle { color: "#FAFAFA" }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0" }
        Text {
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: "设置"; font.pixelSize: 18; font.bold: true; color: "#212121"
        }
    }

    ScrollView {
        anchors.fill: parent; contentWidth: availableWidth

        Column {
            width: Math.min(parent.width - 48, 600)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0; topPadding: 24; bottomPadding: 32

            // ---- Section: 外观 ----
            SectionHeader { title: "外观" }

            SettingRow {
                label: "深色主题"
                control: Switch {
                    checked: root.darkMode
                    onCheckedChanged: { root.darkMode = checked; root.themeToggled(checked) }
                }
            }

            // ---- Section: Agent 工具配置 ----
            SectionHeader { title: "Agent 工具配置" }

            // Tavily API Key row
            Item {
                width: parent.width; height: 52
                Text {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: "Tavily API Key"; font.pixelSize: 14; color: "#424242"
                }
                RowLayout {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 4
                    TextField {
                        id: tavilyKeyField
                        implicitWidth: 260
                        placeholderText: "tvly-xxxxxxxxxxxxxxxx"
                        echoMode: showKey.checked ? TextInput.Normal : TextInput.Password
                        font.pixelSize: 13
                        text: tavilyKeySetting
                        onEditingFinished: {
                            chatEngine.setTavilyKey(text.trim())
                            groupChat.setTavilyKey(text.trim())
                            tavilyKeySetting = text.trim()
                        }
                    }
                    Button {
                        id: showKey
                        checkable: true
                        text: checked ? "隐藏" : "显示"
                        flat: true; font.pixelSize: 12
                        implicitWidth: 48; implicitHeight: 32
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#F5F5F5" }
            }

            // Work Dir row
            Item {
                width: parent.width; height: 52
                Text {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: "Agent 工作目录"; font.pixelSize: 14; color: "#424242"
                }
                TextField {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    implicitWidth: 320
                    placeholderText: "例：C:/Users/me/Workspace"
                    font.pixelSize: 13
                    text: workDirSetting
                    onEditingFinished: {
                        chatEngine.setWorkDir(text.trim())
                        groupChat.setWorkDir(text.trim())
                        workDirSetting = text.trim()
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#F5F5F5" }
            }

            // ---- Section: 统计 ----
            SectionHeader { title: "用量统计" }

            SettingRow {
                label: "累计输入 Token"
                control: Text { text: String(tracker.totalTokensIn); font.pixelSize: 14; color: "#424242" }
            }
            SettingRow {
                label: "累计输出 Token"
                control: Text { text: String(tracker.totalTokensOut); font.pixelSize: 14; color: "#424242" }
            }
            SettingRow {
                label: "累计调用次数"
                control: Text { text: String(tracker.totalCalls); font.pixelSize: 14; color: "#424242" }
            }
            SettingRow {
                label: "清除日志"
                control: Button {
                    text: "清除"; height: 32
                    Material.foreground: "white"; Material.background: "#F44336"
                    onClicked: {
                        tracker.clearLogs()
                        logModel.clear()
                        loadLogs()
                    }
                }
            }

            // ---- Section: 日志 ----
            SectionHeader { title: "调用日志" }

            Rectangle {
                width: parent.width; height: 320
                radius: 8; color: "white"
                border.color: "#E0E0E0"
                clip: true

                ListView {
                    id: logList
                    anchors { fill: parent; margins: 8 }
                    spacing: 4
                    model: ListModel { id: logModel }

                    delegate: Rectangle {
                        width: logList.width; height: 48
                        radius: 6; color: model.error_message ? "#FFF3E0" : "#F5F5F5"
                        Column {
                            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 10 }
                            spacing: 2
                            Text {
                                text: model.event_type + "  " + model.model_name
                                font.pixelSize: 13; font.bold: true; color: "#424242"
                            }
                            Text {
                                text: "in:" + model.tokens_in + "  out:" + model.tokens_out +
                                      "  " + model.created_at +
                                      (model.error_message ? "  ⚠ " + model.error_message : "")
                                font.pixelSize: 11; color: "#9E9E9E"
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: logList.count === 0
                        text: "暂无日志"; color: "#9E9E9E"; font.pixelSize: 14
                    }
                }
            }
        }
    }

    function loadLogs() {
        logModel.clear()
        const logs = tracker.getLogs(100)
        for (const l of logs) logModel.append(l)
    }

    Component.onCompleted: loadLogs()
    Connections { target: tracker; function onStatsChanged() { loadLogs() } }

    component SectionHeader: Item {
        property string title: ""
        width: parent.width; height: 40
        Text {
            anchors { bottom: parent.bottom; bottomMargin: 4 }
            text: title; font.pixelSize: 12; font.bold: true; color: "#1976D2"
            leftPadding: 2
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#E3F2FD" }
    }

    component SettingRow: Item {
        property string label: ""
        property alias control: controlHolder.children
        width: parent.width; height: 52
        Text {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: label; font.pixelSize: 14; color: "#424242"
        }
        Item {
            id: controlHolder
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#F5F5F5" }
    }
}
