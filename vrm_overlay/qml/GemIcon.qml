import QtQuick

Canvas {
    property var commands: []
    property color ink: "#fff8ee"
    onCommandsChanged: requestPaint()
    onInkChanged: requestPaint()
    onPaint: {
        let c = getContext("2d")
        c.reset(); c.scale(width / 28, height / 28); c.translate(2, 2)
        c.strokeStyle = ink; c.lineWidth = 1.7; c.lineCap = "round"; c.lineJoin = "round"
        c.beginPath()
        for (let p of commands) {
            if (p[0] === "M") c.moveTo(p[1], p[2])
            else if (p[0] === "L") c.lineTo(p[1], p[2])
            else if (p[0] === "C") c.bezierCurveTo(p[1], p[2], p[3], p[4], p[5], p[6])
            else if (p[0] === "O") { c.moveTo(p[1] + p[3], p[2]); c.arc(p[1], p[2], p[3], 0, Math.PI * 2) }
            else if (p[0] === "Z") c.closePath()
        }
        c.stroke()
    }
}
