import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    property int editAgentId: -1   // -1 = create mode
    property bool isEditMode: editAgentId >= 0

    signal saved()
    signal cancelled()

    background: Rectangle { color: "#FAFAFA" }

    function loadAgent() {
        if (isEditMode) {
            const a = agentManager.getAgent(editAgentId)
            nameField.text         = a.name          || ""
            providerCombo.setProvider(a.provider     || "openai")
            modelField.text        = a.modelName     || ""
            apiKeyField.text       = a.apiKey        || ""
            baseUrlField.text      = a.baseUrl       || ""
            promptArea.text        = a.systemPrompt  || ""
            tempSlider.value       = a.temperature   !== undefined ? a.temperature : 0.7
            maxTokensSlider.value  = a.maxTokens     !== undefined ? a.maxTokens   : 4096
        } else {
            nameField.text = ""; modelField.text = ""; apiKeyField.text = ""
            baseUrlField.text = ""; promptArea.text = ""
            tempSlider.value = 0.7; maxTokensSlider.value = 4096
        }
    }


    onEditAgentIdChanged: Qt.callLater(loadAgent)
    Component.onCompleted: Qt.callLater(loadAgent)

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0"; border.width: 1 }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Button { text: "← 返回"; flat: true; onClicked: root.cancelled() }
            Text {
                text: isEditMode ? "编辑 Agent" : "新建 Agent"
                font.pixelSize: 18; font.bold: true; color: "#212121"
            }
            Item { Layout.fillWidth: true }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            width: Math.min(parent.width - 48, 600)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0
            topPadding: 24; bottomPadding: 32

            // --- template picker ---
            Row {
                width: parent.width; spacing: 8
                Text { text: "从模板创建:"; font.pixelSize: 13; color: "#757575"; anchors.verticalCenter: parent.verticalCenter }
                ComboBox {
                    id: templateCombo
                    width: 200
                    model: ["不使用模板"].concat(templateStore.templateNames)
                    onActivated: {
                        if (index === 0) return
                        const t = templateStore.getTemplate(currentText)
                        if (!t) return
                        promptArea.text  = t.systemPrompt || ""
                        modelField.text  = t.modelName    || ""
                        tempSlider.value = t.temperature  !== undefined ? t.temperature : 0.7
                        providerCombo.setProvider(t.provider || "openai")
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#E0E0E0" }

            // --- fields ---
            FormField { label: "Agent 名称 *" }
            TextField {
                id: nameField
                width: parent.width
                placeholderText: "例：代码助手"
                font.pixelSize: 14
            }

            FormField { label: "供应商 *" }
            ComboBox {
                id: providerCombo
                width: parent.width
                model: ["openai", "deepseek", "ollama", "其他(OpenAI兼容)"]
                function setProvider(p) {
                    const idx = model.indexOf(p)
                    currentIndex = idx >= 0 ? idx : 0
                }
            }

            FormField { label: "模型名称 *" }
            TextField {
                id: modelField
                width: parent.width
                placeholderText: "例：gpt-4o-mini / deepseek-chat / llama3"
                font.pixelSize: 14
            }

            FormField { label: "API Key" }
            TextField {
                id: apiKeyField
                width: parent.width
                placeholderText: "sk-..."
                echoMode: TextInput.Password
                font.pixelSize: 14
                rightPadding: 40

                Button {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 4 }
                    text: apiKeyField.echoMode === TextInput.Password ? "👁" : "🙈"
                    flat: true; width: 36; height: 36
                    font.pixelSize: 14
                    onClicked: apiKeyField.echoMode =
                        apiKeyField.echoMode === TextInput.Password ? TextInput.Normal : TextInput.Password
                }
            }

            FormField { label: "Base URL (可选)" }
            TextField {
                id: baseUrlField
                width: parent.width
                placeholderText: "留空使用默认，Ollama: http://localhost:11434"
                font.pixelSize: 14
            }

            FormField { label: "系统提示词" }
            Rectangle {
                width: parent.width; height: 120
                radius: 6; color: "white"
                border.color: promptArea.activeFocus ? "#1976D2" : "#E0E0E0"
                Behavior on border.color { ColorAnimation { duration: 150 } }

                ScrollView {
                    anchors { fill: parent; margins: 4 }
                    TextArea {
                        id: promptArea
                        placeholderText: "你是一个有用的 AI 助手..."
                        font.pixelSize: 13
                        wrapMode: TextArea.Wrap
                        background: Item {}
                    }
                }
            }

            FormField { label: "温度: " + tempSlider.value.toFixed(2) }
            Slider {
                id: tempSlider
                width: parent.width
                from: 0; to: 2; stepSize: 0.05; value: 0.7
            }

            FormField { label: "最大 Token: " + maxTokensSlider.value }
            Slider {
                id: maxTokensSlider
                width: parent.width
                from: 256; to: 32768; stepSize: 256; value: 4096
            }

            // validation error
            Text {
                id: errText
                visible: text.length > 0
                color: "#F44336"; font.pixelSize: 13
                topPadding: 8
            }

            // buttons
            Row {
                width: parent.width; spacing: 12; topPadding: 24
                layoutDirection: Qt.RightToLeft

                Button {
                    text: "保存"
                    Material.foreground: "white"
                    Material.background: "#1976D2"
                    onClicked: {
                        errText.text = ""
                        if (nameField.text.trim() === "") {
                            errText.text = "Agent 名称不能为空"; return
                        }
                        if (modelField.text.trim() === "") {
                            errText.text = "模型名称不能为空"; return
                        }
                        const provider = providerCombo.currentText === "其他(OpenAI兼容)"
                                       ? "openai" : providerCombo.currentText

                        let ok
                        if (isEditMode) {
                            ok = agentManager.updateAgent(
                                editAgentId,
                                nameField.text.trim(),
                                provider,
                                modelField.text.trim(),
                                apiKeyField.text.trim(),
                                baseUrlField.text.trim(),
                                promptArea.text,
                                tempSlider.value,
                                maxTokensSlider.value
                            )
                        } else {
                            ok = agentManager.createAgent(
                                nameField.text.trim(),
                                provider,
                                modelField.text.trim(),
                                apiKeyField.text.trim(),
                                baseUrlField.text.trim(),
                                promptArea.text,
                                tempSlider.value,
                                maxTokensSlider.value,
                                templateCombo.currentIndex > 0 ? templateCombo.currentText : ""
                            )
                        }
                        if (ok) root.saved()
                        else errText.text = "保存失败，请检查输入"
                    }
                }
                Button {
                    text: "取消"; flat: true
                    onClicked: root.cancelled()
                }
            }
        }
    }

    // helper label component
    component FormField: Item {
        property string label: ""
        width: parent.width; height: 32
        Text {
            anchors { bottom: parent.bottom; bottomMargin: 2 }
            text: label; font.pixelSize: 12; color: "#757575"
        }
    }
}
