import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "chatSettingsDialog"
    required property var chat
    title: "Chat settings"
    modal: true
    anchors.centerIn: parent
    width: Math.min(620, parent.width - 24)
    height: Math.min(760, parent.height - 24)
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        endpoint.text = chat.config.endpoint
        model.text = chat.config.model
        character.text = chat.config.character
        readAloud.checked = chat.config.read_aloud
        actions.checked = chat.config.actions
        noKey.checked = chat.config.no_key
        apiKey.clear()
    }
    onClosed: apiKey.clear()
    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label { text: "OpenAI-compatible Chat Completions API"; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: "Full endpoint URL" }
            TextField { id: endpoint; objectName: "chatEndpoint"; Layout.fillWidth: true; placeholderText: "https://api.example.com/v1/chat/completions"; selectByMouse: true }
            Label { text: "Model ID" }
            TextField { id: model; objectName: "chatModel"; Layout.fillWidth: true; placeholderText: "Model ID from your provider"; selectByMouse: true }
            Label { text: "New API key" }
            TextField { id: apiKey; objectName: "chatApiKey"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "Leave blank to keep the saved key"; selectByMouse: true }
            Label { text: "Keys go only into the system keyring, separately for each endpoint. They are never saved with these settings."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            Label { text: dialog.chat.credentialStatus; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            RowLayout {
                enabled: !dialog.chat.credentialBusy && endpoint.text === dialog.chat.config.endpoint
                Button { text: "Check saved key"; onClicked: dialog.chat.checkKey() }
                Button { text: "Remove saved key"; onClicked: dialog.chat.removeKey() }
            }
            CheckBox { id: noKey; objectName: "chatNoKey"; text: "No API key (local server only)" }
            CheckBox { id: readAloud; text: "Speak replies using the selected speech profile" }
            CheckBox { id: actions; text: "Allow the character's listed gestures and expressions" }
            Label { text: "Character description"; font.bold: true }
            TextArea { id: character; objectName: "chatCharacter"; Layout.fillWidth: true; Layout.preferredHeight: 170; wrapMode: TextEdit.Wrap; selectByMouse: true }
            Label { text: "Available actions and speech formatting are appended automatically. Check animations in Studio's library to let the LLM use them."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            GroupBox {
                title: "Current system prompt"
                Layout.fillWidth: true
                TextArea { width: parent.width; text: dialog.chat.prompt; textFormat: TextEdit.PlainText; readOnly: true; wrapMode: TextEdit.Wrap; selectByMouse: true; background: null }
            }
            Label { text: dialog.chat.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ffc2c8" }
            Label { text: "Changing the endpoint, model or character starts a new conversation. Chat history stays in memory for this session."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
        }
    }
    footer: DialogButtonBox {
        Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: dialog.close() }
        Button {
            objectName: "saveChatSettingsButton"
            text: dialog.chat.credentialBusy ? "Waiting for keyring…" : "Save"
            enabled: !dialog.chat.credentialBusy
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: {
                if (dialog.chat.saveSettings({endpoint: endpoint.text, model: model.text, character: character.text,
                                             no_key: noKey.checked, read_aloud: readAloud.checked, actions: actions.checked})) {
                    if (apiKey.text.length > 0) dialog.chat.saveKey(apiKey.text)
                    apiKey.clear()
                    dialog.close()
                }
            }
        }
    }
}
