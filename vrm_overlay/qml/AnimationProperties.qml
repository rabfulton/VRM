import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var controller
    property string animationPath: ""
    property string animationName: ""
    property string configError: ""
    objectName: "animationPropertiesDialog"
    title: "Animation properties · " + animationName
    modal: true
    anchors.centerIn: parent
    width: Math.min(470, parent.width - 32)
    standardButtons: Dialog.NoButton
    function edit(entry) {
        animationPath = entry.path
        animationName = entry.name
        loopable.checked = entry.loopable
        startup.checked = entry.startup
        idle.checked = entry.is_idle
        configError = entry.config_error || ""
        open()
    }
    contentItem: ColumnLayout {
        spacing: 12
        CheckBox { id: loopable; objectName: "animationLoopableProperty"; text: "Loopable" }
        Label { text: "Allow this animation to repeat. Turn off for one-shot actions."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
        CheckBox { id: startup; objectName: "animationStartupProperty"; text: "Play at startup" }
        Label { text: "Play once when the avatar loads, then return to an idle. If several are marked, the first by filename is used."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
        CheckBox { id: idle; objectName: "animationIdleProperty"; text: "Idle animation" }
        Label { text: "Eligible between actions. Loopable idles repeat; one-shot idles move on to another idle when finished."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
        Label { text: "Saved beside the animation in its .cfg file. LLM access is controlled separately by the library checkbox."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
        Label { text: dialog.configError; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ffc2c8" }
    }
    footer: DialogButtonBox {
        Button { text: "Cancel"; onClicked: dialog.close() }
        Button {
            objectName: "saveAnimationPropertiesButton"
            text: "Save"
            onClicked: {
                if (dialog.controller.saveAnimationProperties(dialog.animationPath,
                        {loopable: loopable.checked, startup: startup.checked, is_idle: idle.checked}))
                    dialog.close()
                else dialog.configError = dialog.controller.lastError
            }
        }
    }
}
