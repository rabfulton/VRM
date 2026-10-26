import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    id: panel
    required property var speech
    required property bool connected
    clip: true
    contentWidth: availableWidth
    FileDialog {
        id: filePicker
        property string option
        onAccepted: panel.speech.setOption(option, selectedFile.toString())
    }
    ColumnLayout {
        width: parent.width
        spacing: 14
        GroupBox {
            title: "Speech settings"
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                Label { text: "Provider and voice settings are saved separately for each model."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#9eacc0" }
                Label { text: "Provider" }
                ComboBox {
                    objectName: "ttsProviderSelector"
                    Layout.fillWidth: true
                    model: panel.speech.providers; textRole: "name"; valueRole: "id"
                    currentIndex: model.findIndex(item => item.id === panel.speech.config.provider)
                    onActivated: panel.speech.selectProvider(currentValue)
                }
                Label { text: "Model"; visible: panel.speech.config.provider === "crispasr" }
                ComboBox {
                    objectName: "ttsModelSelector"
                    visible: panel.speech.config.provider === "crispasr"
                    Layout.fillWidth: true
                    model: panel.speech.models; textRole: "name"; valueRole: "id"
                    currentIndex: model.findIndex(item => item.id === panel.speech.config.model)
                    displayText: currentIndex >= 0 ? currentText : panel.speech.config.model
                    onActivated: panel.speech.selectModel(currentValue)
                }
                RowLayout {
                    visible: panel.speech.config.provider === "crispasr"
                    Label { text: "Processing" }
                    ComboBox {
                        objectName: "ttsDeviceSelector"
                        Layout.preferredWidth: 240
                        model: ["Vulkan GPU (default)", "CPU", "Automatic"]
                        currentIndex: ["vulkan", "cpu", "auto"].indexOf(panel.speech.config.device)
                        onActivated: panel.speech.setOption("device", ["vulkan", "cpu", "auto"][currentIndex])
                    }
                }
                Label {
                    visible: panel.speech.config.provider === "crispasr"
                    text: "Prepare downloads the native engine and selected model when missing. Nano needs about 700 MB of model files; Turbo about 1 GB. An installed engine or local model files can be selected below."
                    wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#9eacc0"
                }
                RowLayout {
                    Button { objectName: "prepareSpeechButton"; text: "Prepare voice"; visible: panel.speech.config.provider === "crispasr"; enabled: !panel.speech.busy; onClicked: panel.speech.prepare() }
                    Button { text: "Stop"; enabled: panel.speech.busy; onClicked: panel.speech.stop() }
                    BusyIndicator { running: panel.speech.busy; implicitWidth: 28; implicitHeight: 28; visible: running }
                }
            }
        }
        GroupBox {
            title: "Try the voice"
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                TextArea {
                    id: previewText
                    objectName: "ttsPreviewText"
                    Layout.fillWidth: true
                    implicitHeight: 90
                    text: "Hello! I'm ready to help."
                    placeholderText: "Write something for the avatar to say…"
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                }
                RowLayout {
                    Button { objectName: "speakPreviewButton"; text: "Speak"; enabled: panel.connected && !panel.speech.busy && previewText.text.trim().length > 0; onClicked: panel.speech.speak(previewText.text) }
                    Button { objectName: "stopPreviewButton"; text: "Stop speech"; enabled: panel.connected || panel.speech.busy; onClicked: panel.speech.stop() }
                    Label { text: panel.speech.status; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
                }
                Label { text: panel.speech.error; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#ffc2c8" }
                Label { text: "Lip sync follows the generated audio automatically."; color: "#9eacc0" }
            }
        }
        Button { id: advanced; text: checked ? "Hide advanced settings" : "Advanced settings…"; checkable: true }
        GroupBox {
            title: "Local engine and model files"
            visible: advanced.checked
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 3
                Label { text: "Executable" }
                TextField { objectName: "ttsExecutableField"; Layout.fillWidth: true; text: panel.speech.config.executable; placeholderText: panel.speech.config.provider === "piper" ? "piper" : "Automatic / crispasr on PATH"; onEditingFinished: panel.speech.setOption("executable", text) }
                Button { text: "Browse…"; onClicked: { filePicker.option = "executable"; filePicker.open() } }
                Label { text: "Model file" }
                TextField { objectName: "ttsModelPathField"; Layout.fillWidth: true; text: panel.speech.config.model_path; placeholderText: panel.speech.config.provider === "piper" ? "Voice .onnx (required)" : "Automatic download / cached model"; onEditingFinished: panel.speech.setOption("model_path", text) }
                Button { text: "Browse…"; onClicked: { filePicker.option = "model_path"; filePicker.open() } }
                Label { text: "Decoder file"; visible: panel.speech.config.provider === "crispasr" }
                TextField { Layout.fillWidth: true; visible: panel.speech.config.provider === "crispasr"; text: panel.speech.config.codec_path; placeholderText: "Automatic companion model"; onEditingFinished: panel.speech.setOption("codec_path", text) }
                Button { text: "Browse…"; visible: panel.speech.config.provider === "crispasr"; onClicked: { filePicker.option = "codec_path"; filePicker.open() } }
                Label { text: "Voice pack / name"; visible: panel.speech.config.provider === "crispasr" }
                TextField { Layout.fillWidth: true; visible: panel.speech.config.provider === "crispasr"; text: panel.speech.config.voice; placeholderText: "Engine's default voice"; onEditingFinished: panel.speech.setOption("voice", text) }
                Button { text: "Browse…"; visible: panel.speech.config.provider === "crispasr"; onClicked: { filePicker.option = "voice"; filePicker.open() } }
                Label { text: "Language"; visible: panel.speech.config.provider === "crispasr" }
                TextField { Layout.fillWidth: true; visible: panel.speech.config.provider === "crispasr"; text: panel.speech.config.language; onEditingFinished: panel.speech.setOption("language", text) }
                Item { visible: panel.speech.config.provider === "crispasr" }
                Label { text: "CPU threads"; visible: panel.speech.config.provider === "crispasr" }
                SpinBox { visible: panel.speech.config.provider === "crispasr"; from: 1; to: 64; value: panel.speech.config.threads; onValueModified: panel.speech.setOption("threads", value) }
                Item { visible: panel.speech.config.provider === "crispasr" }
                Label { text: "Vulkan device index"; visible: panel.speech.config.provider === "crispasr" && panel.speech.config.device === "vulkan" }
                SpinBox { visible: panel.speech.config.provider === "crispasr" && panel.speech.config.device === "vulkan"; from: 0; to: 32; value: panel.speech.config.gpu_device; onValueModified: panel.speech.setOption("gpu_device", value) }
                Item { visible: panel.speech.config.provider === "crispasr" && panel.speech.config.device === "vulkan" }
                Label { text: "Other CrispASR backend"; visible: panel.speech.config.provider === "crispasr" }
                TextField { Layout.fillWidth: true; visible: panel.speech.config.provider === "crispasr"; text: panel.speech.config.model; placeholderText: "Backend ID supported by your CrispASR build"; onEditingFinished: { if (text !== panel.speech.config.model) panel.speech.selectModel(text) } }
                Item { visible: panel.speech.config.provider === "crispasr" }
            }
        }
    }
}
