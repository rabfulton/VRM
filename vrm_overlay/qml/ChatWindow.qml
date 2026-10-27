import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "chatWindow"
    required property var chat
    width: 440
    height: 600
    minimumWidth: 360
    minimumHeight: 420
    visible: false
    transientParent: null
    title: "domovushka chat"
    color: "#111822"
    palette.disabled.buttonText: "#8793a5"
    property bool followReply: true

    function followLatest() {
        if (followReply) followTimer.restart()
    }
    Timer {
        id: followTimer
        interval: 0
        onTriggered: {
            if (window.followReply) {
                transcript.forceLayout()
                transcript.positionViewAtEnd()
            }
        }
    }

    function showChat() {
        show()
        raise()
        requestActivate()
        composer.forceActiveFocus()
        followLatest()
    }
    function sendMessage() {
        if (composer.text.trim().length && chat.config.model.length && !chat.credentialBusy) {
            chat.send(composer.text)
            composer.clear()
            followReply = true
            followLatest()
        }
    }
    Connections {
        target: window.chat
        function onWindowRequested() { window.showChat() }
        function onNearCursorRequested(anchor) {
            window.x = Math.max(anchor.left, Math.min(anchor.x + 16, anchor.right - window.width))
            window.y = Math.max(anchor.top, Math.min(anchor.y - window.height / 2, anchor.bottom - window.height))
            window.showChat()
        }
        function onMessagesChanged() {
            window.followLatest()
        }
    }
    Shortcut { sequence: "Escape"; enabled: window.active && !settings.visible; onActivated: window.hide() }
    ChatSettings { id: settings; chat: window.chat; parent: Overlay.overlay }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { text: "CONVERSATION"; font.bold: true; font.letterSpacing: 1.5 }
            Item { Layout.fillWidth: true }
            ToolButton { objectName: "newChatButton"; text: "New"; onClicked: window.chat.newConversation() }
            ToolButton { objectName: "chatSettingsButton"; text: "Settings"; onClicked: settings.open() }
        }
        Label {
            visible: !window.chat.config.model.length
            text: "Choose an API endpoint and model in Settings to start chatting."
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0"
        }
        ListView {
            id: transcript
            objectName: "chatTranscript"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 14
            model: window.chat.messages
            ScrollBar.vertical: ScrollBar {
                onPressedChanged: {
                    if (pressed) window.followReply = false
                    else if (transcript.atYEnd) window.followReply = true
                }
            }
            onMovementStarted: window.followReply = false
            onMovementEnded: { if (atYEnd) window.followReply = true }
            onContentHeightChanged: window.followLatest()
            onHeightChanged: window.followLatest()
            onWidthChanged: window.followLatest()
            delegate: Frame {
                id: bubble
                objectName: "chatBubble" + index
                required property var modelData
                required property int index
                width: transcript.width
                padding: 12
                height: implicitHeight
                onHeightChanged: window.followLatest()
                background: Rectangle { color: modelData.role === "user" ? "#243246" : "#17212f"; radius: 10 }
                contentItem: ColumnLayout {
                    spacing: 6
                    Label { text: modelData.role === "user" ? "You" : "Companion"; color: "#52cbb4"; font.bold: true; font.pixelSize: 12 }
                    TextArea {
                        objectName: "chatMessageText" + bubble.index
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: modelData.text
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        background: null
                        padding: 0
                    }
                    Label { objectName: "chatMessageState" + bubble.index; visible: modelData.state.length > 0; text: modelData.state; color: "#9eacc0"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
                }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: window.chat.messages.length === 0
                text: "Ask a question or say hello.\nReplies can speak and animate your companion."
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "#9eacc0"
            }
        }
        Label { text: window.chat.error; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ffc2c8" }
        ScrollView {
            objectName: "chatComposerArea"
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            clip: true
            contentWidth: availableWidth
            TextArea {
                id: composer
                objectName: "chatInput"
                placeholderText: "Ask your companion…"
                placeholderTextColor: "#9eacc0"
                color: "#e5edf6"
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                Accessible.name: "Chat message"
                background: Rectangle { color: "#17212f"; radius: 8; border.color: "#35445a" }
                Keys.onReturnPressed: event => {
                    if (!(event.modifiers & Qt.ShiftModifier)) {
                        window.sendMessage()
                        event.accepted = true
                    } else event.accepted = false
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: window.chat.status; color: "#9eacc0"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { objectName: "stopChatButton"; text: "Stop"; enabled: window.chat.busy; onClicked: window.chat.stop() }
            Button {
                objectName: "sendChatButton"
                text: window.chat.busy ? "Interrupt & send" : "Send"
                enabled: composer.text.trim().length > 0 && window.chat.config.model.length > 0 && !window.chat.credentialBusy
                onClicked: window.sendMessage()
            }
        }
        Label { text: "Enter sends · Shift+Enter adds a line · Escape hides"; color: "#9eacc0"; font.pixelSize: 11 }
    }
}
