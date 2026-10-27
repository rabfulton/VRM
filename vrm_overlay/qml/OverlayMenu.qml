import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: menu
    objectName: "overlayMenu"
    required property var controller
    property var palette
    property var font
    transientParent: null
    // A tool window with an explicit input grab avoids Qt's popup-stack replay
    // sending the cancelling right-click back to the separate GLFW process.
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    width: 180
    height: actions.implicitHeight + 12
    color: "#17212f"
    visible: false
    Component.onCompleted: controller.registerOverlayMenu(menu)
    onActiveChanged: { if (!active) hide() }
    Shortcut { sequence: "Escape"; enabled: menu.visible; onActivated: menu.hide() }
    Connections {
        target: menu.controller
        function onOverlayMenuRequested(position) {
            if (menu.visible) { menu.hide(); return }
            // All coordinates, including workarea bounds, come from Qt so they
            // remain in the same logical coordinate space on scaled displays.
            menu.x = position.x ?? 0
            menu.y = position.y ?? 0
            menu.show()
            let left = position.left ?? menu.screen.virtualX
            let top = position.top ?? menu.screen.virtualY
            let right = position.right ?? (left + menu.screen.width)
            let bottom = position.bottom ?? (top + menu.screen.height)
            menu.x = Math.max(left, Math.min(menu.x, right - menu.width))
            menu.y = Math.max(top, Math.min(menu.y, bottom - menu.height))
            menu.raise()
            menu.requestActivate()
        }
    }
    Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#35445a"; radius: 6 }
    ColumnLayout {
        id: actions
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
        spacing: 2
        Button {
            objectName: "overlayChatButton"
            text: "Chat"
            palette: menu.palette
            font: menu.font
            Layout.fillWidth: true
            onClicked: { menu.hide(); menu.controller.chat.openNearCursor() }
        }
        Button {
            objectName: "overlayStudioButton"
            text: "Studio"
            palette: menu.palette
            font: menu.font
            Layout.fillWidth: true
            onClicked: { menu.hide(); menu.controller.showStudio() }
        }
        CheckDelegate {
            objectName: "overlayAlwaysOnTopCheck"
            text: "Always on top"
            palette: menu.palette
            font: menu.font
            Layout.fillWidth: true
            checked: menu.controller.state.always_on_top
            enabled: menu.controller.connected
            onToggled: {
                menu.hide()
                menu.controller.command("set_render", {always_on_top: checked})
            }
        }
        Button {
            objectName: "overlayQuitButton"
            text: "Quit"
            palette: menu.palette
            font: menu.font
            Layout.fillWidth: true
            onClicked: { menu.hide(); menu.controller.quitApplication() }
        }
    }
}
