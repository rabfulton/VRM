import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    objectName: "avatarImportDialog"
    required property var controller
    readonly property var library: controller.avatars
    property var selected: ({})
    property string importSource: ""
    readonly property var rows: tabs.currentIndex === 0 ? library.featured : library.installed
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    width: Math.min(980, parent.width - 40)
    height: Math.min(740, parent.height - 40)
    padding: 22
    modal: true
    closePolicy: library.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: "#17212f"; radius: 12; border.color: "#35445a" }
    onOpened: {
        library.refresh()
        selected = rows.find(entry => entry.id === selected.id) || (rows.length ? rows[0] : ({}))
    }
    function chooseFile(sourceId) {
        importSource = sourceId
        fileDialog.open()
    }
    function chooseInstalled(id) {
        tabs.currentIndex = 1
        for (let entry of library.installed) {
            if (entry.id === id) { selected = entry; return }
        }
    }
    Connections {
        target: dialog.library
        function onImported(id) { dialog.chooseInstalled(id) }
    }
    FileDialog {
        id: fileDialog
        title: "Import downloaded avatar"
        nameFilters: ["Avatars and packages (*.vrm *.glb *.gltf *.zip)", "All files (*)"]
        onAccepted: dialog.library.importFile(selectedFile.toString(), dialog.importSource)
    }
    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 4
                    Label { text: "Choose your companion"; font.pixelSize: 25; font.bold: true }
                    Label { text: "Find an avatar or bring one of your own."; color: "#9eacc0" }
                }
                Item { Layout.fillWidth: true }
                ToolButton { objectName: "closeAvatarDialog"; text: "✕"; Accessible.name: "Close avatar library"; enabled: !dialog.library.busy; onClicked: dialog.close() }
            }
            RowLayout {
                Layout.fillWidth: true
                TabBar {
                    id: tabs
                    Layout.preferredWidth: 320
                    TabButton { objectName: "featuredAvatarsTab"; text: "Discover" }
                    TabButton { objectName: "installedAvatarsTab"; text: "My avatars (" + dialog.library.installed.length + ")" }
                    onCurrentIndexChanged: { dialog.selected = dialog.rows.length ? dialog.rows[0] : ({}) }
                }
                Item { Layout.fillWidth: true }
                Button { objectName: "importAvatarFileButton"; text: "Import file…"; enabled: !dialog.library.busy; onClicked: dialog.chooseFile("") }
            }
            Frame {
                visible: dialog.library.choices.length > 0
                Layout.fillWidth: true
                background: Rectangle { color: "#243246"; radius: 6 }
                ColumnLayout {
                    anchors.fill: parent
                    Label { text: "This ZIP has several models. Choose an outfit or variant:"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        ComboBox { id: variants; objectName: "avatarVariantPicker"; model: dialog.library.choices; Layout.fillWidth: true; enabled: !dialog.library.busy }
                        Button { objectName: "importAvatarVariantButton"; text: "Import variant"; enabled: !dialog.library.busy && variants.currentIndex >= 0; onClicked: dialog.library.importVariant(variants.currentText) }
                        ToolButton { text: "Cancel"; enabled: !dialog.library.busy; onClicked: dialog.library.cancelVariants() }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 18
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    GridView {
                        id: grid
                        objectName: "avatarGrid"
                        anchors.fill: parent
                        clip: true
                        model: dialog.rows
                        cellWidth: width / Math.max(1, Math.floor(width / 175))
                        cellHeight: 254
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item {
                            required property var modelData
                            width: grid.cellWidth
                            height: grid.cellHeight
                            ItemDelegate {
                                objectName: "avatarCard_" + modelData.id
                                anchors.fill: parent
                                anchors.rightMargin: 10
                                anchors.bottomMargin: 10
                                Accessible.name: modelData.name + " by " + modelData.author
                                onClicked: dialog.selected = modelData
                                padding: 10
                                background: Rectangle {
                                    color: parent.hovered ? "#293c50" : "#1d2a3b"
                                    radius: 8
                                    border.width: dialog.selected.id === modelData.id ? 2 : 1
                                    border.color: dialog.selected.id === modelData.id ? "#52cbb4" : "#35445a"
                                }
                                contentItem: ColumnLayout {
                                    spacing: 5
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: 5
                                        color: "#111822"
                                        Label { anchors.centerIn: parent; text: modelData.name.substring(0, 1); font.pixelSize: 48; color: "#657d98"; visible: thumbnail.status !== Image.Ready }
                                        Image {
                                            id: thumbnail
                                            anchors.fill: parent
                                            anchors.margins: 3
                                            source: dialog.visible ? modelData.thumbnail : ""
                                            sourceSize.width: 320
                                            sourceSize.height: 320
                                            asynchronous: true
                                            fillMode: Image.PreserveAspectFit
                                        }
                                    }
                                    Label { text: modelData.name; textFormat: Text.PlainText; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: modelData.author; textFormat: Text.PlainText; color: "#9eacc0"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: modelData.badge; color: "#72dbc6"; font.pixelSize: 12 }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        visible: dialog.rows.length === 0
                        Label { text: "Make yourself at home"; font.pixelSize: 21; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                        Label { text: "Drop a VRM or ZIP here, or import a file.\nYour avatars will appear here after import."; color: "#9eacc0"; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: "#35445a" }
                ScrollView {
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 12
                        Label { text: dialog.selected.name || "Your avatar library"; textFormat: Text.PlainText; font.pixelSize: 23; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Label { text: dialog.selected.author ? "By " + dialog.selected.author : ""; textFormat: Text.PlainText; color: "#9eacc0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Label { text: dialog.selected.description || "Import an avatar to get started."; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Label { text: dialog.selected.license || ""; textFormat: Text.PlainText; color: "#9eacc0"; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Label { text: dialog.selected.instructions || ""; textFormat: Text.PlainText; color: "#c3d1e2"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Button {
                            objectName: "useAvatarButton"
                            visible: !!dialog.selected.path
                            Layout.fillWidth: true
                            text: dialog.controller.connected ? "Use avatar" : "Connect and use avatar"
                            highlighted: true
                            enabled: !dialog.library.busy && !dialog.controller.busy
                            onClicked: { dialog.controller.useAvatar(dialog.selected.path, dialog.selected.rig); dialog.close() }
                        }
                        Button {
                            objectName: "avatarCreatorButton"
                            visible: dialog.selected.kind === "featured" || dialog.selected.kind === "bundled" || !!dialog.selected.source_id
                            Layout.fillWidth: true
                            text: dialog.selected.kind === "featured" ? "Creator’s download page ↗" : "Creator and license ↗"
                            onClicked: dialog.library.openCreator(dialog.selected.source_id || dialog.selected.id)
                        }
                        Button {
                            text: "Embedded license ↗"
                            visible: dialog.selected.kind === "installed" && (dialog.selected.license_url || "").startsWith("https://")
                            Layout.fillWidth: true
                            onClicked: dialog.library.openTerms(dialog.selected.id)
                            ToolTip.visible: hovered
                            ToolTip.text: dialog.selected.license_url || ""
                        }
                        Button {
                            objectName: "importSelectedAvatarButton"
                            visible: dialog.selected.kind === "featured"
                            Layout.fillWidth: true
                            text: "Import downloaded file…"
                            highlighted: true
                            enabled: !dialog.library.busy
                            onClicked: dialog.chooseFile(dialog.selected.id)
                        }
                        Label {
                            visible: dialog.selected.kind === "featured"
                            text: "1  Open the creator’s page\n2  Review terms and download\n3  Import the VRM or ZIP here"
                            lineHeight: 1.4
                            font.pixelSize: 12
                            color: "#9eacc0"
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Button {
                            objectName: "importCurrentAvatarButton"
                            text: "Add current avatar to library"
                            visible: !dialog.library.installed.some(entry => entry.path === dialog.controller.modelPath) && !dialog.library.featured.some(entry => entry.path === dialog.controller.modelPath)
                            enabled: !dialog.library.busy && dialog.controller.modelPath.length > 0
                            Layout.fillWidth: true
                            onClicked: dialog.library.importFile(dialog.controller.modelPath, "")
                        }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#35445a" }
            RowLayout {
                Layout.fillWidth: true
                BusyIndicator { running: dialog.library.busy; visible: running; Layout.preferredWidth: 26; Layout.preferredHeight: 26 }
                Label {
                    text: dialog.library.error || dialog.library.status || "Drop a VRM, GLB, glTF or ZIP anywhere in this dialog."
                    textFormat: Text.PlainText
                    color: dialog.library.error ? "#ffc2c8" : "#9eacc0"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            Label { text: "Stored in " + dialog.library.folder; textFormat: Text.PlainText; color: "#7b8ba1"; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
        }
        DropArea {
            anchors.fill: parent
            enabled: !dialog.library.busy
            onEntered: function(drag) { drag.accepted = drag.hasUrls && drag.urls.length === 1 }
            onDropped: function(drop) {
                if (drop.hasUrls && drop.urls.length === 1) {
                    dialog.library.importFile(drop.urls[0].toString(), "")
                    drop.acceptProposedAction()
                }
            }
            Rectangle {
                anchors.fill: parent
                visible: parent.containsDrag
                radius: 8
                color: "#de17212f"
                border.color: "#52cbb4"
                border.width: 2
                Label { anchors.centerIn: parent; text: "Drop to import your avatar"; font.pixelSize: 24; color: "#72dbc6" }
            }
        }
    }
}
