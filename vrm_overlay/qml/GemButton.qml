import QtQuick
import QtQuick.Controls

AbstractButton {
    id: gem
    required property var entry
    required property var theme
    required property var commands
    readonly property real radius: entry.radius
    objectName: entry.objectName || ("gemAction_" + entry.id)
    text: entry.label
    enabled: entry.enabled
    checked: entry.checked
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    width: 126
    height: radius * 2 + 34
    Accessible.name: text
    Accessible.description: checked ? "Enabled" : ""
    opacity: enabled ? 1 : 0.42
    onHoveredChanged: body.requestPaint()
    onActiveFocusChanged: body.requestPaint()
    onCheckedChanged: body.requestPaint()
    onThemeChanged: body.requestPaint()
    background: Item {}
    contentItem: Item {
        Canvas {
            id: body
            x: -20; y: -20
            width: parent.width + 40
            height: gem.radius * 2 + 50
            onPaint: {
                let c = getContext("2d"); c.reset()
                let x = width / 2, y = gem.radius + 22, r = gem.radius
                let hot = gem.hovered || gem.activeFocus
                function hex(radius) {
                    c.beginPath()
                    for (let i = 0; i < 6; i++) {
                        let a = -Math.PI / 2 + i * Math.PI / 3
                        let px = x + radius * Math.cos(a), py = y + radius * Math.sin(a)
                        if (!i) c.moveTo(px, py); else c.lineTo(px, py)
                    }
                    c.closePath()
                }
                for (let i = 4; i > 0; i--) {
                    hex(r + i * 1.5)
                    c.globalAlpha = hot ? .07 : .035
                    c.strokeStyle = gem.theme.primary; c.lineWidth = i * 3; c.stroke()
                }
                c.globalAlpha = 1; hex(r)
                let gradient = c.createLinearGradient(x, y-r, x, y+r)
                gradient.addColorStop(0, gem.entry.danger && hot ? "#b44d6d" : gem.theme.primary)
                gradient.addColorStop(1, gem.theme.face)
                c.fillStyle = gradient; c.fill()
                c.strokeStyle = gem.theme.accent; c.lineWidth = hot || gem.checked ? 2.4 : 1.4; c.stroke()
                hex(r - 5); c.globalAlpha = hot ? .8 : .4; c.lineWidth = .8; c.stroke()
                c.beginPath(); c.moveTo(x, y-r+5); c.lineTo(x, y); c.lineTo(x-r*.77, y-r*.45); c.closePath()
                c.fillStyle = "#ffffff"; c.globalAlpha = .10; c.fill()
                c.beginPath(); c.moveTo(x, y); c.lineTo(x+r*.77, y+r*.45); c.lineTo(x, y+r-5); c.closePath()
                c.globalAlpha = .06; c.fill(); c.globalAlpha = 1
            }
        }
        GemIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            y: gem.radius - height / 2 + 2
            width: gem.entry.hero ? 45 : 37; height: width
            ink: gem.theme.ink; commands: gem.commands
        }
        Rectangle {
            visible: gem.checked
            x: parent.width / 2 + gem.radius * .43; y: 7
            width: 19; height: 19; radius: 9.5; color: gem.theme.accent
            Text { anchors.centerIn: parent; text: "✓"; color: gem.theme.face; font.pixelSize: 13; font.bold: true }
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: gem.radius * 2 + 5
            width: Math.min(126, caption.implicitWidth + 18); height: 24; radius: 12
            color: gem.theme.shadow
            border.color: gem.hovered || gem.activeFocus ? gem.theme.accent : "#554b3b58"
            Text {
                id: caption
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                text: gem.text; color: gem.theme.ink
                font.family: "Sans Serif"; font.pixelSize: 13; font.weight: gem.entry.hero ? Font.DemiBold : Font.Medium
                elide: Text.ElideRight
            }
        }
    }
}
