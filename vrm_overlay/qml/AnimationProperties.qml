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
        floorContact.checked = entry.floor_contact ?? true
        entryPose.currentIndex = entryPose.model.indexOf(entry.entry_pose || "standing")
        exitPose.currentIndex = exitPose.model.indexOf(entry.exit_pose || "standing")
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
        Label { text: "Eligible between actions in the matching pose. Several idles take turns; a lone loopable idle repeats."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
        RowLayout {
            Label { text: "Starts"; color: "#9eacc0" }
            ComboBox { id: entryPose; objectName: "animationEntryPoseProperty"; model: ["standing", "sitting", "lying", "crouching"]; Layout.fillWidth: true }
            Label { text: "Ends"; color: "#9eacc0" }
            ComboBox { id: exitPose; objectName: "animationExitPoseProperty"; model: entryPose.model; Layout.fillWidth: true }
        }
        Label { text: "Automatic actions use one-shot transitions between these poses. Manual playback remains direct."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
        CheckBox { id: floorContact; objectName: "animationFloorContactProperty"; text: "Allow floor foot locking" }
        Label { text: "Turn off for climbing, sitting, lying and motions that should keep their authored foot movement."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
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
                        {loopable: loopable.checked, startup: startup.checked, is_idle: idle.checked,
                         entry_pose: entryPose.currentText, exit_pose: exitPose.currentText, floor_contact: floorContact.checked}))
                    dialog.close()
                else dialog.configError = dialog.controller.lastError
            }
        }
    }
}
