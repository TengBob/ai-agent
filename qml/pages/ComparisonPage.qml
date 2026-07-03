import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    background: Rectangle { color: "#FAFAFA" }

    property var selectedAgents: []
    property int summaryAgentId: -1

    Component.onCompleted: {
        comparisonEngine.agentResult.connect(onAgentResult)
        comparisonEngine.summaryReady.connect(onSummaryReady)
    }
    Component.onDestruction: {
        comparisonEngine.agentResult.disconnect(onAgentResult)
        comparisonEngine.summaryReady.disconnect(onSummaryReady)
    }

    function onAgentResult(agentId, agentName, modelName, content, error, tokens) {
        for (let i = 0; i < resultModel.count; i++) {
            if (resultModel.get(i).agentId === agentId) {
                resultModel.set(i, {
                    agentId:   agentId,
                    agentName: agentName,
                    modelName: modelName,
                    content:   content,
                    error:     error,
                    tokens:    tokens,
                    loading:   false
                })
                return
            }
        }
    }

    function onSummaryReady(summary, error) {
        summaryText.text   = summary || ("⚠ " + error)
        summaryText.color  = error ? "#C62828" : "#212121"
        summaryLoading.visible = false
        summaryCard.visible    = true
    }

    function startComparison() {
        if (selectedAgents.length === 0) return
        const q = questionArea.text.trim()
        if (q.length === 0) return

        resultModel.clear()
        summaryCard.visible    = false
        summaryLoading.visible = false

        for (const id of selectedAgents) {
            const agent = agentManager.getAgent(id)
            resultModel.append({
                agentId:   id,
                agentName: agent.name      || ("Agent " + id),
                modelName: agent.modelName || "",
                content:   "",
                error:     "",
                tokens:    0,
                loading:   true
            })
        }

        if (summaryAgentId > 0) summaryLoading.visible = true

        comparisonEngine.startComparison(selectedAgents, q, summaryAgentId)
    }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0" }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Text {
                text: "多模型对比"
                font.pixelSize: 18; font.bold: true; color: "#212121"
                Layout.fillWidth: true
            }
            Rectangle {
                visible: comparisonEngine.isRunning
                width: 72; height: 30; radius: 6
                color: cancelHdr.containsMouse ? "#C62828" : "#E53935"
                Text { anchors.centerIn: parent; text: "取消"; color: "white"; font.pixelSize: 12 }
                MouseArea { id: cancelHdr; anchors.fill: parent; hoverEnabled: true
                    onClicked: comparisonEngine.cancel() }
            }
        }
    }

    ListModel { id: resultModel }

    // ── SplitView：左侧固定配置栏 | 右侧结果栏 ──
    SplitView {
        anchors { fill: parent; margins: 8 }
        orientation: Qt.Horizontal

        // ══════════════ 左侧配置面板 ══════════════
        Rectangle {
            SplitView.preferredWidth: 260
            SplitView.minimumWidth:   200
            SplitView.maximumWidth:   340
            color: "transparent"

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                // --- 对比 Agent ---
                Text { text: "对比 Agent"; font.pixelSize: 13; font.bold: true; color: "#424242" }

                Rectangle {
                    Layout.fillWidth: true
                    height: 150
                    radius: 8; color: "white"; border.color: "#E0E0E0"; clip: true
                    ListView {
                        anchors { fill: parent; margins: 6 }
                        spacing: 2
                        model: agentManager.agentListModel
                        delegate: CheckDelegate {
                            width: parent.width
                            text: model.agentName + "\n" + model.agentModelName
                            font.pixelSize: 12
                            onCheckedChanged: {
                                const id = model.agentId
                                if (checked) {
                                    if (!selectedAgents.includes(id))
                                        selectedAgents = selectedAgents.concat([id])
                                } else {
                                    selectedAgents = selectedAgents.filter(x => x !== id)
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: parent.count === 0
                            text: "没有 Agent"; color: "#9E9E9E"; font.pixelSize: 12
                        }
                    }
                }

                // --- 总结 Agent ---
                Text { text: "总结 Agent（可选）"; font.pixelSize: 13; font.bold: true; color: "#424242" }

                Rectangle {
                    Layout.fillWidth: true
                    height: 110
                    radius: 8; color: "white"; border.color: "#E0E0E0"; clip: true
                    ListView {
                        anchors { fill: parent; margins: 6 }
                        spacing: 2
                        model: agentManager.agentListModel
                        delegate: Rectangle {
                            width: parent.width; height: 34; radius: 4
                            color: summaryAgentId === model.agentId ? "#E3F2FD"
                                   : (sh.containsMouse ? "#F5F5F5" : "white")
                            Text {
                                anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                                text: (summaryAgentId === model.agentId ? "✔ " : "") + model.agentName
                                font.pixelSize: 12
                                color: summaryAgentId === model.agentId ? "#1976D2" : "#424242"
                            }
                            MouseArea { id: sh; anchors.fill: parent; hoverEnabled: true
                                onClicked: summaryAgentId = (summaryAgentId === model.agentId ? -1 : model.agentId) }
                        }
                        Text {
                            anchors.centerIn: parent; visible: parent.count === 0
                            text: "没有 Agent"; color: "#9E9E9E"; font.pixelSize: 12
                        }
                    }
                }

                // --- 问题输入 ---
                Text { text: "问题"; font.pixelSize: 13; font.bold: true; color: "#424242" }

                Rectangle {
                    Layout.fillWidth: true
                    height: 100
                    radius: 8; color: "white"
                    border.color: questionArea.activeFocus ? "#1976D2" : "#E0E0E0"

                    ScrollView {
                        anchors { fill: parent; margins: 4 }
                        TextArea {
                            id: questionArea
                            wrapMode: TextArea.Wrap
                            font.pixelSize: 13
                            background: Item {}
                            Keys.onReturnPressed: (event) => {
                                if (event.modifiers & Qt.ShiftModifier) {
                                    event.accepted = false
                                } else {
                                    startComparison()
                                    event.accepted = true
                                }
                            }
                            Keys.onEnterPressed: (event) => {
                                startComparison()
                                event.accepted = true
                            }
                        }
                    }
                    Text {
                        anchors { left: parent.left; top: parent.top; margins: 8 }
                        text: "输入问题，按 Enter 开始..."
                        font.pixelSize: 13; color: "#BDBDBD"
                        visible: !questionArea.activeFocus && questionArea.text.length === 0
                        MouseArea { anchors.fill: parent; onClicked: questionArea.forceActiveFocus() }
                    }
                }

                // --- 开始按钮 ---
                Rectangle {
                    Layout.fillWidth: true; height: 40; radius: 8
                    property bool ok: selectedAgents.length >= 1
                                      && questionArea.text.trim().length > 0
                                      && !comparisonEngine.isRunning
                    color: startMa.containsMouse && ok ? "#1565C0" : (ok ? "#1976D2" : "#BDBDBD")
                    Text {
                        anchors.centerIn: parent
                        text: comparisonEngine.isRunning ? "对比中..." : "开始对比"
                        color: "white"; font.pixelSize: 13; font.bold: true
                    }
                    MouseArea { id: startMa; anchors.fill: parent; hoverEnabled: true
                        enabled: parent.ok; onClicked: startComparison() }
                }

                Text {
                    visible: selectedAgents.length < 1
                    text: "请至少选择 1 个 Agent"
                    color: "#9E9E9E"; font.pixelSize: 11; wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ══════════════ 右侧结果区 ══════════════
        Item {
            SplitView.fillWidth: true

            // 空状态
            Column {
                anchors.centerIn: parent
                spacing: 12
                visible: resultModel.count === 0 && !summaryLoading.visible && !summaryCard.visible
                Text { anchors.horizontalCenter: parent.horizontalCenter
                    text: "⚖️"; font.pixelSize: 48 }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "选择 Agent 并输入问题\n按 Enter 或点击「开始对比」"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14; color: "#BDBDBD"; lineHeight: 1.7
                }
            }

            // 结果滚动区（纵向堆叠）
            ScrollView {
                anchors.fill: parent
                clip: true
                visible: resultModel.count > 0 || summaryLoading.visible || summaryCard.visible
                contentWidth: availableWidth

                Column {
                    id: resultsColumn
                    width: parent.width
                    spacing: 10
                    topPadding: 4; bottomPadding: 4

                    // 每个 Agent 的结果卡片
                    Repeater {
                        model: resultModel
                        delegate: Rectangle {
                            width: resultsColumn.width - 8
                            height: 240
                            radius: 10; color: "white"
                            border.color: model.error ? "#FFCDD2" : "#E0E0E0"

                            ColumnLayout {
                                anchors { fill: parent; margins: 12 }
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: model.agentName
                                        font.pixelSize: 14; font.bold: true; color: "#212121"
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: model.modelName
                                        font.pixelSize: 11; color: "#9E9E9E"
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: "#EEEEEE" }

                                BusyIndicator {
                                    visible: model.loading
                                    Layout.alignment: Qt.AlignHCenter
                                    running: model.loading
                                }

                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    visible: !model.loading

                                    TextArea {
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextArea.Wrap
                                        font.pixelSize: 13
                                        color: model.error ? "#C62828" : "#212121"
                                        text: model.error   ? ("⚠ " + model.error)
                                            : model.content ? model.content
                                            : "等待中..."
                                        background: Item {}
                                        padding: 0
                                    }
                                }

                                Text {
                                    visible: !model.loading && !model.error && model.tokens > 0
                                    text: "tokens: " + model.tokens
                                    font.pixelSize: 11; color: "#BDBDBD"
                                }
                            }
                        }
                    }

                    // 总结加载中
                    Rectangle {
                        width: resultsColumn.width - 8
                        height: 60
                        visible: summaryLoading.visible
                        radius: 10; color: "#FFFDE7"; border.color: "#FFE082"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                            BusyIndicator {
                                id: summaryLoading
                                visible: false; running: visible
                                implicitWidth: 32; implicitHeight: 32
                            }
                            Text { text: "正在生成总结..."; font.pixelSize: 13; color: "#F57F17" }
                        }
                    }

                    // 综合总结卡片
                    Rectangle {
                        id: summaryCard
                        width: resultsColumn.width - 8
                        height: Math.max(120, summaryText.implicitHeight + 60)
                        visible: false
                        radius: 10; color: "#FFFDE7"; border.color: "#FFE082"

                        ColumnLayout {
                            anchors { fill: parent; margins: 14 }
                            spacing: 8

                            Text {
                                text: "✦ 综合总结"
                                font.pixelSize: 14; font.bold: true; color: "#F57F17"
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#FFE082" }

                            TextArea {
                                id: summaryText
                                Layout.fillWidth: true
                                readOnly: true; selectByMouse: true
                                wrapMode: TextArea.Wrap
                                font.pixelSize: 13; color: "#212121"
                                background: Item {}
                                padding: 0
                            }
                        }
                    }
                }
            }
        }
    }
}
