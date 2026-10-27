import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var controller
    property string animationPath: ""
    property string animationName: ""
    property string configError: ""
    property bool animationEnabled: true
    objectName: "animationPropertiesDialog"
    title: "Animation properties · " + animationName
    modal: true
    anchors.centerIn: parent
    width: Math.min(470, parent.width - 32)
    height: Math.min(740, parent.height - 32)
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
        animationEnabled = entry.enabled ?? true
        llmAccess.checked = entry.llm_enabled
        open()
    }
    contentItem: ScrollView {
        id: propertiesScroll
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: propertiesScroll.availableWidth
            spacing: 12
            CheckBox { id: llmAccess; objectName: "animationLlmAccessProperty"; text: "Available to LLM" }
            Label { text: "Include this animation in the character's capabilities when it is enabled in the library sidebar."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            Label { visible: !dialog.animationEnabled; text: "Automatic use is disabled. You can still click its name to preview once. Check the sidebar box to allow startup, idle, transitions or permitted LLM use."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#f0ce8b" }
            CheckBox { id: loopable; objectName: "animationLoopableProperty"; text: "Loopable" }
            Label { text: "Allow this animation to repeat. Turn off for one-shot actions."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            CheckBox { id: startup; objectName: "animationStartupProperty"; text: "Play at startup" }
            Label { text: "Play once when the avatar loads, then return to an idle. If several are marked, the first by filename is used."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            CheckBox { id: idle; objectName: "animationIdleProperty"; text: "Idle animation" }
            Label { text: "Eligible between actions in the matching pose. Several idles take turns; a lone loopable idle repeats."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            RowLayout {
                Label { text: "Starts in"; color: "#9eacc0" }
                ComboBox { id: entryPose; objectName: "animationEntryPoseProperty"; model: ["standing", "sitting", "lying", "crouching"]; Layout.fillWidth: true }
                Label { text: "Ends in"; color: "#9eacc0" }
                ComboBox { id: exitPose; objectName: "animationExitPoseProperty"; model: entryPose.model; Layout.fillWidth: true }
            }
            Label { text: "Describe the clip's body poses; these do not trim or change the animation. Example: sit down starts standing and ends sitting. Automatic actions use these labels to chain transitions."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            CheckBox { id: floorContact; objectName: "animationFloorContactProperty"; text: "Allow floor foot locking" }
            Label { text: "Turn off for climbing, sitting, lying and motions that should keep their authored foot movement."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            Label { text: "Properties, including LLM access, are saved in the animation's .cfg file. The sidebar checkbox enables automatic use; manual previews are always available."; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#9eacc0" }
            Label { text: dialog.configError; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#ffc2c8" }
        }
    }
    footer: DialogButtonBox {
        Button { text: "Cancel"; onClicked: dialog.close() }
        Button {
            objectName: "saveAnimationPropertiesButton"
            text: "Save"
            onClicked: {
                if (dialog.controller.saveAnimationProperties(dialog.animationPath,
                        {loopable: loopable.checked, startup: startup.checked, is_idle: idle.checked, llm_enabled: llmAccess.checked,
                         entry_pose: entryPose.currentText, exit_pose: exitPose.currentText, floor_contact: floorContact.checked}))
                    dialog.close()
                else dialog.configError = dialog.controller.lastError
            }
        }
    }
}
