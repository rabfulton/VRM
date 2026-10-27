import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    objectName: "studioWindow"
    width: 1120
    height: 840
    minimumWidth: 940
    minimumHeight: 680
    visible: bridge.studioVisible
    onClosing: function(close) { close.accepted = false; bridge.hideStudio() }
    title: "VRM Studio"
    color: "#111822"
    palette.window: "#111822"
    palette.windowText: "#e5edf6"
    palette.base: "#17212f"
    palette.alternateBase: "#1d2a3b"
    palette.text: "#e5edf6"
    palette.button: "#243246"
    palette.buttonText: "#e5edf6"
    palette.highlight: "#52cbb4"
    palette.highlightedText: "#0e211e"
    palette.mid: "#35445a"
    font.family: "Sans Serif"
    font.pixelSize: 14
    property var runtime: bridge.state
    property var physics: runtime.physics || ({garment_collisions: false, garment_chains: 0, arm_colliders: 0, generated_arm_colliders: 0, contacts: 0, unresolved_contacts: 0, max_penetration: 0, garment_arm_penetration: 0})
    property var planting: runtime.foot_lock || ({enabled: false, supported: 0, pelvis_offset: 0, left: {supported: false, planted: false, weight: 0, error: 0}, right: {supported: false, planted: false, weight: 0, error: 0}})
    property bool canControl: bridge.connected && runtime.model_loaded
    property var playingEntry: bridge.animationFiles.find(entry => entry.path === runtime.animation_path)
    property var filteredAnimations: {
        let result = []
        let files = bridge.animationFiles
        for (let i = 0; i < files.length; i++) {
            if (files[i].name.toLowerCase().includes(search.text.toLowerCase()))
                result.push({ name: files[i].name, file: files[i].file, path: files[i].path,
                              llm_enabled: files[i].llm_enabled, llm_id: files[i].llm_id, sourceIndex: i })
        }
        return result
    }
    function footStatus(foot) { return !foot.supported ? "unmapped" : foot.planted ? (foot.weight >= 0.99 ? "planted" : "planting") : foot.weight > 0 ? "releasing" : "free" }
    function seconds(value) { return Number(value).toFixed(2) + "s" }

    ChatWindow { id: chatWindow; chat: bridge.chat; palette: window.palette; font: window.font }
    OverlayMenu { controller: bridge; palette: window.palette; font: window.font }
    Connections {
        target: bridge
        function onStudioRequested() { window.show(); window.raise(); window.requestActivate() }
    }
    AnimationProperties { id: animationProperties; controller: bridge; parent: Overlay.overlay }
    Shortcut {
        sequence: "Ctrl+Return"
        context: Qt.ApplicationShortcut
        onActivated: chatWindow.visible ? chatWindow.hide() : bridge.chat.open()
    }

    AvatarImportDialog { id: avatarDialog; controller: bridge }
    FolderDialog {
        id: folderDialog
        title: "Choose animation library"
        onAccepted: bridge.setAnimationFolder(selectedFolder.toString())
    }
    FileDialog {
        id: audioDialog
        title: "Play audio with lip sync"
        nameFilters: ["Audio (*.wav *.mp3 *.flac *.ogg)", "All files (*)"]
        onAccepted: bridge.playAudio(selectedFile.toString())
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 3
                Label { text: "VRM STUDIO"; font.pixelSize: 23; font.bold: true; font.letterSpacing: 2 }
                Label { text: "Animation & behaviour workspace"; color: "#9eacc0" }
            }
            Item { Layout.fillWidth: true }
            Button { objectName: "openChatButton"; text: "Chat…"; onClicked: bridge.chat.open(); ToolTip.visible: hovered; ToolTip.text: "Chat with your companion (Ctrl+Enter)" }
            Button { objectName: "resumeIdleButton"; text: "Resume idle"; enabled: window.canControl; onClicked: bridge.resumeIdle() }
            BusyIndicator { running: bridge.busy; visible: running; Layout.preferredWidth: 30; Layout.preferredHeight: 30 }
            Rectangle { width: 8; height: 8; radius: 4; color: bridge.connected ? "#52cbb4" : "#8793a5" }
            Label { text: bridge.status; Layout.maximumWidth: 240; elide: Text.ElideRight }
            Button {
                objectName: "connectionButton"
                text: bridge.connected ? "Disconnect" : "Connect"
                onClicked: bridge.connected ? bridge.disconnectRuntime() : bridge.connectRuntime()
            }
        }
        Frame {
            visible: bridge.lastError.length > 0
            Layout.fillWidth: true
            background: Rectangle { color: "#3b2528"; radius: 6; border.color: "#89515a" }
            RowLayout {
                anchors.fill: parent
                Label { text: bridge.lastError; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#ffc2c8" }
                ToolButton { text: "Dismiss"; onClicked: bridge.clearError() }
            }
        }
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 14; color: "transparent" }
            Frame {
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 240
                SplitView.maximumWidth: 420
                background: Rectangle { color: "#17212f"; radius: 8 }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Label { text: "AVATAR"; font.bold: true; color: "#9eacc0"; font.letterSpacing: 1.5 }
                    Label {
                        text: window.runtime.model_path.length ? window.runtime.model_path.split("/").pop() : "No avatar loaded"
                        font.pixelSize: 18; font.bold: true; Layout.fillWidth: true; elide: Text.ElideMiddle
                    }
                    Button {
                        objectName: "loadModelButton"
                        text: "Choose / import avatar…"; Layout.fillWidth: true
                        onClicked: avatarDialog.open()
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#304055" }
                    RowLayout {
                        Label { text: "ANIMATION LIBRARY"; font.bold: true; color: "#9eacc0"; Layout.fillWidth: true }
                        ToolButton { text: "Folder…"; onClicked: folderDialog.open() }
                        ToolButton { text: "↻"; Accessible.name: "Refresh animation library"; onClicked: bridge.scanAnimations() }
                    }
                    Label { text: bridge.animationFolder; font.pixelSize: 11; color: "#9eacc0"; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    TextField { id: search; objectName: "animationSearch"; placeholderText: "Search animations"; placeholderTextColor: "#9eacc0"; Layout.fillWidth: true }
                    ListView {
                        id: animationList
                        objectName: "animationList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: window.filteredAnimations
                        currentIndex: window.filteredAnimations.findIndex(item => item.sourceIndex === bridge.selectedAnimation)
                        onCurrentIndexChanged: { if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain) }
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ItemDelegate {
                            required property var modelData
                            width: ListView.view.width
                            highlighted: bridge.selectedAnimation === modelData.sourceIndex
                            Accessible.name: "Play " + modelData.name
                            onClicked: { if (window.canControl && !bridge.busy) bridge.playAnimation(modelData.sourceIndex) }
                            contentItem: RowLayout {
                                spacing: 6
                                CheckBox {
                                    objectName: "llmAnimationCheck" + modelData.sourceIndex
                                    checked: modelData.llm_enabled
                                    indicator: Rectangle {
                                        implicitWidth: 20; implicitHeight: 20
                                        x: parent.leftPadding; y: (parent.height - height) / 2
                                        radius: 4
                                        color: parent.checked ? "#52cbb4" : "#111822"
                                        border.color: parent.activeFocus ? "#e5edf6" : "#8793a5"
                                        Text { anchors.centerIn: parent; text: "✓"; color: "#0e211e"; font.bold: true; visible: parent.parent.checked }
                                    }
                                    onToggled: bridge.setAnimationLlmEnabled(modelData.sourceIndex, checked)
                                    Accessible.name: "Allow " + modelData.name + " for LLM"
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Allow LLM to use [[animation:" + modelData.llm_id + "]]"
                                }
                                Label {
                                    text: modelData.name
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    enabled: window.canControl && !bridge.busy
                                }
                                ToolButton {
                                    objectName: "animationPropertiesButton" + modelData.sourceIndex
                                    text: "⋯"
                                    Accessible.name: "Properties for " + modelData.name
                                    onClicked: animationProperties.edit(bridge.animationFiles[modelData.sourceIndex])
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Looping, startup and idle properties"
                                }
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.file
                        }
                        Label {
                            anchors.centerIn: parent
                            width: parent.width
                            visible: animationList.count === 0
                            text: bridge.animationFiles.length ? "No matching animations" : "Choose a folder containing\n.vrma or .glb animations"
                            color: "#9eacc0"; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        }
                    }
                    Label { text: animationList.count + " animations · " + bridge.llmAnimationCount + " enabled for LLM"; color: "#9eacc0"; font.pixelSize: 12 }
                    Label { text: "Click a name to play · check to allow LLM"; color: "#9eacc0"; font.pixelSize: 11 }
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            objectName: "previousAnimationButton"
                            text: "← Previous"; Layout.fillWidth: true
                            enabled: window.canControl && !bridge.busy && bridge.animationFiles.length > 0
                            onClicked: bridge.stepAnimation(-1)
                        }
                        Button {
                            objectName: "nextAnimationButton"
                            text: "Next →"; Layout.fillWidth: true
                            enabled: window.canControl && !bridge.busy && bridge.animationFiles.length > 0
                            onClicked: bridge.stepAnimation(1)
                        }
                    }
                }
            }
            ColumnLayout {
                SplitView.fillWidth: true
                spacing: 14
                Frame {
                    Layout.fillWidth: true
                    background: Rectangle { color: "#17212f"; radius: 8 }
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        Label { text: "PLAYBACK"; color: "#9eacc0"; font.bold: true; font.letterSpacing: 1.5 }
                        Label {
                            text: window.runtime.animation_path.length ? window.runtime.animation_path.split("/").pop() : "Select an animation to begin"
                            Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 22; font.bold: true
                        }
                        Label { text: window.runtime.blend.active ? "Transition " + Math.round(window.runtime.blend.progress * 100) + "%" : (window.runtime.backend === "ozz" ? "Velocity-preserving transitions" : "Legacy sampling"); color: "#52cbb4"; font.pixelSize: 12 }
                        ComboBox {
                            Layout.fillWidth: true
                            visible: window.runtime.clips.length > 1
                            model: window.runtime.clips
                            textRole: "name"
                            currentIndex: window.runtime.animation_index
                            onActivated: bridge.command("blend_to", {index: currentIndex, duration: window.runtime.blend.duration, loop: window.runtime.loop})
                        }
                        Slider {
                            id: timeline
                            objectName: "timeline"
                            Layout.fillWidth: true
                            enabled: window.canControl && window.runtime.duration > 0
                            from: 0; to: Math.max(0.01, window.runtime.duration)
                            onMoved: bridge.tune("set_time", "t", value)
                            Accessible.name: "Animation time"
                            Binding { target: timeline; property: "value"; value: window.runtime.time; when: !timeline.pressed }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                objectName: "playPauseButton"
                                text: window.runtime.playing ? "Pause" : "Play"
                                enabled: window.canControl && window.runtime.duration > 0 && !bridge.busy
                                onClicked: bridge.command(window.runtime.playing ? "pause" : "play", {speed: window.runtime.speed})
                            }
                            Label { text: window.seconds(window.runtime.time) + " / " + window.seconds(window.runtime.duration); color: "#9eacc0"; font.family: "monospace" }
                            Item { Layout.fillWidth: true }
                            ComboBox {
                                objectName: "speedSelector"
                                model: ["0.5×", "1×", "1.5×", "2×"]
                                currentIndex: [0.5, 1, 1.5, 2].indexOf(window.runtime.speed)
                                enabled: window.canControl
                                onActivated: bridge.command("set_playback", {speed: [0.5, 1, 1.5, 2][currentIndex]})
                            }
                            Switch {
                                objectName: "loopSwitch"
                                text: "Loop"; checked: window.runtime.loop; enabled: window.canControl && (!window.playingEntry || window.playingEntry.loopable)
                                onToggled: bridge.command("set_playback", {loop: checked})
                            }
                        }
                    }
                }
                TabBar {
                    id: tabs
                    objectName: "workspaceTabs"
                    Layout.fillWidth: true
                    TabButton { text: "Behaviours" }
                    TabButton { text: "Physics" }
                    TabButton { text: "Animation" }
                    TabButton { text: "View" }
                    TabButton { text: "Speech" }
                    TabButton { text: "Log" }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabs.currentIndex
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: 16
                            GroupBox {
                                title: "Eyes & speech"
                                Layout.fillWidth: true
                                enabled: window.canControl
                                ColumnLayout {
                                    anchors.fill: parent
                                    Switch {
                                        objectName: "autoBlinkSwitch"
                                        text: "Automatic blinking"; checked: window.runtime.blink.enabled
                                        onToggled: bridge.command("auto_blink_enable", {enabled: checked})
                                    }
                                    TuningSlider { Layout.fillWidth: true; label: "Blinks per minute"; value: window.runtime.blink.rate; minimum: 1; maximum: 60; step: 1; decimals: 0; onEdited: value => bridge.tune("auto_blink_config", "rate", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Blink duration (ms)"; value: window.runtime.blink.duration; minimum: 50; maximum: 500; step: 10; decimals: 0; onEdited: value => bridge.tune("auto_blink_config", "duration", value) }
                                    Label { text: "Lip sync follows audio automatically."; color: "#9eacc0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                    RowLayout {
                                        Button { text: "Play audio…"; onClicked: audioDialog.open() }
                                        Button { text: "Speech settings…"; onClicked: tabs.currentIndex = 4 }
                                        Button { text: "Stop speech"; onClicked: bridge.speech.stop() }
                                    }
                                }
                            }
                            GroupBox {
                                title: "Expressions"
                                Layout.fillWidth: true
                                enabled: window.canControl
                                ColumnLayout {
                                    anchors.fill: parent
                                    Label { text: "Choose an expression, set its weight, then apply. Clear resets all expressions."; wrapMode: Text.Wrap; color: "#9eacc0"; Layout.fillWidth: true }
                                    ComboBox { id: expression; objectName: "expressionSelector"; model: window.runtime.expressions; Layout.fillWidth: true }
                                    Slider { id: expressionWeight; from: 0; to: 1; value: 1; stepSize: 0.05; Layout.fillWidth: true; Accessible.name: "Expression weight" }
                                    RowLayout {
                                        Label { text: "Weight " + expressionWeight.value.toFixed(2); color: "#9eacc0" }
                                        Item { Layout.fillWidth: true }
                                        Button { objectName: "applyExpressionButton"; text: "Apply"; enabled: expression.currentIndex >= 0; onClicked: bridge.command("set_expression", {name: expression.currentText, weight: expressionWeight.value}) }
                                        Button { text: "Clear all"; onClicked: bridge.command("clear_expressions", {}) }
                                    }
                                }
                            }
                        }
                    }
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: 16
                            enabled: window.canControl
                            GroupBox {
                                title: "Spring motion"
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Switch { objectName: "garmentCollisionSwitch"; text: "Arm collisions for skirts & clothing"; checked: window.physics.garment_collisions; enabled: window.runtime.physics !== undefined; onToggled: bridge.command("set_spring", {garment_collisions: checked}) }
                                    Label { text: window.physics.garment_chains + " garment chains · " + window.physics.arm_colliders + " arm volumes (" + window.physics.generated_arm_colliders + " generated)"; color: "#9eacc0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                    Label { text: window.physics.contacts + " contacts · residual overlap " + (window.physics.max_penetration * 1000).toFixed(1) + " mm"; color: window.physics.max_penetration > 0.005 ? "#ffc2c8" : "#9eacc0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                    Label { text: "Garment/arm overlap " + ((window.physics.garment_arm_penetration || 0) * 1000).toFixed(1) + " mm"; color: (window.physics.garment_arm_penetration || 0) > 0.005 ? "#ffc2c8" : "#9eacc0" }
                                    TuningSlider { Layout.fillWidth: true; label: "Stiffness"; value: window.runtime.spring.stiffness; maximum: 5; onEdited: value => bridge.tune("set_spring", "stiffness", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Drag"; value: window.runtime.spring.drag; maximum: 3; onEdited: value => bridge.tune("set_spring", "drag", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Gravity"; value: window.runtime.spring.gravity; maximum: 5; onEdited: value => bridge.tune("set_spring", "gravity", value) }
                                    Switch { text: "Clothing spring profile"; checked: window.runtime.spring.cloth_mode; onToggled: bridge.command("set_spring", {cloth_mode: checked}) }
                                    TuningSlider { Layout.fillWidth: true; label: "Clothing stiffness"; value: window.runtime.spring.cloth_stiffness; maximum: 3; onEdited: value => bridge.tune("set_spring", "cloth_stiffness", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Clothing drag"; value: window.runtime.spring.cloth_drag; maximum: 3; onEdited: value => bridge.tune("set_spring", "cloth_drag", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Clothing gravity"; value: window.runtime.spring.cloth_gravity; maximum: 3; onEdited: value => bridge.tune("set_spring", "cloth_gravity", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Clothing taper"; value: window.runtime.spring.cloth_taper; onEdited: value => bridge.tune("set_spring", "cloth_taper", value) }
                                    RowLayout {
                                        Button { text: "Reset springs"; onClicked: bridge.command("reset_spring", {}) }
                                        Button { text: "Save for this animation"; enabled: window.runtime.animation_path.length > 0; onClicked: bridge.command("save_tuning", {}); ToolTip.visible: hovered; ToolTip.text: "Save spring and seam settings alongside the animation" }
                                    }
                                }
                            }
                        }
                    }
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: 16
                            enabled: window.canControl
                            GroupBox {
                                title: "Transitions & coordinates"
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Switch { objectName: "footLockSwitch"; text: "Foot planting"; checked: window.planting.enabled; enabled: window.runtime.foot_lock !== undefined && window.planting.supported > 0; onToggled: bridge.command("set_foot_lock", {enabled: checked}) }
                                    Label { text: window.planting.supported > 0 ? "Left: " + window.footStatus(window.planting.left) + " · Right: " + window.footStatus(window.planting.right) : "No supported humanoid leg chains"; color: "#9eacc0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                    Label { visible: window.planting.clip_allowed === false; text: "Floor locking is released for this animation by its properties."; color: "#9eacc0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                    Label { text: "Plant error L/R: " + (window.planting.left.planted ? (window.planting.left.error * 1000).toFixed(1) : "—") + " / " + (window.planting.right.planted ? (window.planting.right.error * 1000).toFixed(1) : "—") + " mm"; color: "#9eacc0" }
                                    ComboBox { model: window.runtime.ozz_available ? ["ozz", "legacy"] : ["legacy"]; currentIndex: window.runtime.ozz_available && window.runtime.backend === "legacy" ? 1 : 0; onActivated: bridge.command("set_anim_backend", {backend: currentText}) }
                                    TuningSlider { Layout.fillWidth: true; label: "Transition duration (s)"; value: window.runtime.blend.duration; maximum: 3; onEdited: value => bridge.tune("set_blend", "duration", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Loop seam blend (s)"; value: window.runtime.blend.seam; onEdited: value => bridge.tune("set_blend", "seam", value) }
                                    ComboBox { model: ["No axis correction", "Automatic", "Rotate X 180°", "Rotate Y 180°", "Rotate Z 180°"]; currentIndex: window.runtime.correction; Layout.fillWidth: true; onActivated: bridge.command("set_anim_correction", {mode: currentIndex}) }
                                    Label { text: "Joint-group transition time multipliers"; font.bold: true }
                                    Label { text: "1 = normal duration · 0.5 = twice as fast · 2 = twice as long. Smooth transitions require the ozz backend."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#9eacc0" }
                                    Repeater {
                                        model: ["Root", "Spine", "Head", "Arms", "Hands", "Legs", "Feet", "Face", "Other"]
                                        TuningSlider {
                                            required property string modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            label: modelData; value: window.runtime.blend.groups[index]; maximum: 2
                                            onEdited: value => { bridge.tune("set_blend", "group", index); bridge.tune("set_blend", "factor", value) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: parent.width
                            spacing: 16
                            enabled: bridge.connected
                            GroupBox {
                                title: "Overlay & camera"
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Switch { text: "Show avatar"; checked: window.runtime.visible; onToggled: bridge.command("set_visible", {visible: checked}) }
                                    Switch { text: "Always on top"; checked: window.runtime.always_on_top; onToggled: bridge.command("set_render", {always_on_top: checked}) }
                                    Switch { text: "Fit overlay to animation"; checked: window.runtime.auto_resize; onToggled: bridge.command("set_auto_resize", {enabled: checked}) }
                                    ComboBox { model: ["anime", "toon", "pbr"]; currentIndex: ["anime", "toon", "pbr"].indexOf(window.runtime.shader); onActivated: bridge.command("set_render", {shader: currentText}) }
                                    TuningSlider { Layout.fillWidth: true; label: "Avatar scale"; value: window.runtime.scale; minimum: 0.1; maximum: 3; onEdited: value => bridge.tune("set_render", "scale", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Camera distance"; value: window.runtime.camera.dist; minimum: 0.5; maximum: 30; onEdited: value => bridge.tune("set_camera", "dist", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Camera orbit"; value: window.runtime.camera.azimuth; maximum: 6.28; onEdited: value => bridge.tune("set_camera", "azimuth", value) }
                                    TuningSlider { Layout.fillWidth: true; label: "Camera elevation"; value: window.runtime.camera.polar; minimum: 0.1; maximum: 3; onEdited: value => bridge.tune("set_camera", "polar", value) }
                                    Label { text: "Drag the avatar to move it. Middle-drag rotates the camera; scroll changes its scale."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#9eacc0" }
                                    Label {
                                        property var timing: window.runtime.frame_timing || ({})
                                        text: "Frame " + Number(timing.frame_ms || 0).toFixed(1) + " ms · peak " + Number(timing.peak_frame_ms || 0).toFixed(1) + " ms · frames over 50 ms: " + (timing.slow_frames || 0) +
                                              "\nSlowest frame: IPC " + Number(timing.peak_ipc_ms || 0).toFixed(1) + " / update " + Number(timing.peak_update_ms || 0).toFixed(1) + " / render " + Number(timing.peak_render_ms || 0).toFixed(1) + " / swap " + Number(timing.peak_swap_ms || 0).toFixed(1) + " ms" +
                                              "\nLast clip load " + Number(timing.last_load_ms || 0).toFixed(1) + " ms (bounds " + Number(timing.last_bounds_ms || 0).toFixed(1) + " ms) · window resizes: " + (timing.window_resizes || 0)
                                        wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#9eacc0"
                                    }
                                    Button { text: "Reset frame timings"; enabled: bridge.connected; onClicked: bridge.command("reset_frame_timing", {}) }
                                }
                            }
                        }
                    }
                    SpeechPanel { speech: bridge.speech; connected: window.canControl }
                    ScrollView {
                        TextArea { text: bridge.logText; readOnly: true; wrapMode: TextEdit.Wrap; font.family: "monospace"; font.pixelSize: 12; selectByMouse: true }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: bridge.socketPath; color: "#9eacc0"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Label { text: bridge.connected ? Math.round(window.runtime.frame_rate) + " fps · " + window.runtime.backend : "Ready to connect"; color: "#9eacc0"; font.pixelSize: 12 }
            Label { text: "Close to hide · Right-click avatar to reopen"; color: "#9eacc0"; font.pixelSize: 12 }
        }
    }
}
