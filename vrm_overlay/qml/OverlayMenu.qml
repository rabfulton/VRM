import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: menu
    objectName: "overlayMenu"
    required property var controller
    property var font
    readonly property var design: controller.gemMenu
    readonly property var page: design.pages[design.currentPage] || ({items: [], group: "main", title: "Companion"})
    readonly property var siblings: design.pages.map((p, i) => ({page: p, index: i})).filter(p => p.page.group === page.group)
    readonly property int siblingIndex: siblings.findIndex(p => p.index === design.currentPage)
    property real opening: 1
    property real menuScale: 1
    property string openedContext: ""
    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    width: Math.round(448 * menuScale); height: Math.round(472 * menuScale)
    color: "transparent"
    visible: false
    Component.onCompleted: controller.registerOverlayMenu(menu)
    onActiveChanged: { if (!active) hide() }
    onVisibleChanged: {
        if (visible) { opening = 0; appear.restart() }
        else appear.stop()
    }
    NumberAnimation { id: appear; target: menu; property: "opening"; to: 1; duration: 150; easing.type: Easing.OutCubic }
    Shortcut { sequence: "Escape"; enabled: menu.visible; onActivated: menu.hide() }
    Shortcut { sequence: "Left"; enabled: menu.visible && siblingIndex > 0; onActivated: design.setPage(siblings[siblingIndex - 1].index) }
    Shortcut { sequence: "Right"; enabled: menu.visible && siblingIndex + 1 < siblings.length; onActivated: design.setPage(siblings[siblingIndex + 1].index) }
    Connections {
        target: menu.controller
        function onOverlayMenuRequested(position) {
            if (menu.visible) { menu.hide(); return }
            design.navigate("main")
            menu.openedContext = design.spec.context
            let left = position.left ?? 0, top = position.top ?? 0
            let right = position.right ?? (left + (menu.screen ? menu.screen.width : 1920))
            let bottom = position.bottom ?? (top + (menu.screen ? menu.screen.height : 1080))
            menu.menuScale = Math.min(1, (right-left) / 448, (bottom-top) / 472)
            menu.x = Math.round(Math.max(left, Math.min((position.x ?? 0) - menu.width / 2, right - menu.width)))
            menu.y = Math.round(Math.max(top, Math.min((position.y ?? 0) - 220 * menu.menuScale, bottom - menu.height)))
            menu.show(); menu.raise(); menu.requestActivate(); core.forceActiveFocus()
        }
    }
    Connections {
        target: menu.design
        function onChanged() {
            if (menu.visible && menu.openedContext !== menu.design.spec.context) menu.hide()
            orbit.requestPaint()
        }
    }
    Item {
        width: 448; height: 472
        x: (menu.width-width) / 2; y: (menu.height-height) / 2
        opacity: Math.min(1, menu.opening * 1.5)
        scale: menu.menuScale * (.92 + .08 * menu.opening)
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton; onPressed: menu.hide() }
        Canvas {
            id: orbit
            anchors.fill: parent
            onPaint: {
                let c = getContext("2d"); c.reset()
                let cx = 224, cy = 220, theme = menu.design.theme
                let halo = c.createRadialGradient(cx, cy, 30, cx, cy, 188)
                halo.addColorStop(0, "#33201430"); halo.addColorStop(1, "#00201430")
                c.fillStyle = halo; c.fillRect(0, 0, width, height)
                for (let pair of [[129,.27,1],[137,.65,2],[148,.25,1]]) {
                    c.beginPath(); c.arc(cx, cy, pair[0], 0, Math.PI * 2)
                    c.strokeStyle = theme.accent; c.globalAlpha = pair[1]; c.lineWidth = pair[2]; c.stroke()
                }
                for (let e of menu.page.items) {
                    let dx = e.x-cx, dy = e.y-cy, d = Math.sqrt(dx*dx+dy*dy)
                    c.beginPath(); c.moveTo(cx+dx*52/d, cy+dy*52/d)
                    c.lineTo(e.x-dx*(e.radius+5)/d, e.y-dy*(e.radius+5)/d)
                    c.globalAlpha = .55; c.strokeStyle = theme.accent; c.lineWidth = 1; c.stroke()
                }
                c.globalAlpha = .8; c.strokeStyle = theme.accent; c.lineWidth = 1
                for (let s of [[224,49,4],[363,235,3],[85,235,3],[286,103,3],[163,337,3]]) {
                    c.beginPath(); c.moveTo(s[0]-s[2],s[1]); c.lineTo(s[0]+s[2],s[1]); c.moveTo(s[0],s[1]-s[2]); c.lineTo(s[0],s[1]+s[2]); c.stroke()
                }
            }
            Connections { target: menu; function onPageChanged() { orbit.requestPaint() } }
        }
        Repeater {
            model: menu.page.items
            delegate: GemButton {
                required property var modelData
                entry: modelData; theme: modelData.previewPalette || menu.design.theme
                commands: menu.design.icons[modelData.icon] || []
                x: modelData.x - width / 2; y: modelData.y - radius - 2
                onClicked: { if (modelData.close) menu.hide(); menu.design.activate(modelData.id) }
            }
        }
        GemButton {
            id: core
            entry: ({id: "core", label: menu.page.group === "main" ? "Close" : "Back", enabled: true,
                     checked: false, radius: 43, hero: false, objectName: "gemMenuCore"})
            theme: menu.design.theme
            commands: menu.design.icons[menu.page.group === "main" ? "avatar" : "back"]
            x: 224 - width / 2; y: 220 - radius - 2
            onClicked: menu.page.group === "main" ? menu.hide() : menu.design.navigate("main")
        }
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 140; height: 30
            y: 439; visible: menu.siblings.length > 1
            Button {
                objectName: "gemPreviousPage"; x: 1; width: 40; height: 28; text: "‹"
                Accessible.name: "Previous menu page"
                enabled: menu.siblingIndex > 0; opacity: enabled ? 1 : .3
                background: Rectangle { color: menu.design.theme.shadow; border.color: menu.design.theme.accent; radius: 14 }
                contentItem: Text { text: "‹"; color: menu.design.theme.ink; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: menu.design.setPage(menu.siblings[menu.siblingIndex - 1].index)
            }
            Label { anchors.centerIn: parent; text: (menu.siblingIndex + 1) + " / " + menu.siblings.length; color: menu.design.theme.ink; font.pixelSize: 12 }
            Button {
                objectName: "gemNextPage"; x: 99; width: 40; height: 28; text: "›"
                Accessible.name: "Next menu page"
                enabled: menu.siblingIndex + 1 < menu.siblings.length; opacity: enabled ? 1 : .3
                background: Rectangle { color: menu.design.theme.shadow; border.color: menu.design.theme.accent; radius: 14 }
                contentItem: Text { text: "›"; color: menu.design.theme.ink; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                onClicked: menu.design.setPage(menu.siblings[menu.siblingIndex + 1].index)
            }
        }
    }
}
