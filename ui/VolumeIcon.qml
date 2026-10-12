import QtQuick

Canvas {
    id: icon
    implicitWidth: 20
    implicitHeight: 20
    property bool muted: false
    property color iconColor: "#dbe7f5"

    onMutedChanged: requestPaint()
    onIconColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        ctx.save()
        ctx.scale(width / 24, height / 24)
        ctx.fillStyle = iconColor
        ctx.strokeStyle = iconColor
        ctx.lineWidth = 2
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        // A solid speaker body stays legible at the small sizes used in track heads.
        ctx.beginPath()
        ctx.moveTo(3, 9)
        ctx.lineTo(7.5, 9)
        ctx.lineTo(13, 4.5)
        ctx.lineTo(13, 19.5)
        ctx.lineTo(7.5, 15)
        ctx.lineTo(3, 15)
        ctx.closePath()
        ctx.fill()

        if (muted) {
            ctx.beginPath()
            ctx.moveTo(17, 9)
            ctx.lineTo(22, 15)
            ctx.moveTo(22, 9)
            ctx.lineTo(17, 15)
            ctx.stroke()
        } else {
            ctx.beginPath()
            ctx.arc(12.5, 12, 5, -0.72, 0.72)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(12.5, 12, 8.5, -0.72, 0.72)
            ctx.stroke()
        }
        ctx.restore()
    }
}
