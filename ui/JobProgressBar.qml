import QtQuick
import QtQuick.Controls

ProgressBar {
    id: bar
    implicitHeight: 8
    from: 0
    to: 1
    background: Rectangle {
        radius: height / 2
        color: "#344252"
    }
    contentItem: Item {
        clip: true
        Rectangle {
            visible: !bar.indeterminate
            width: parent.width * Math.max(0, Math.min(1, bar.position))
            height: parent.height
            radius: height / 2
            color: "#238aff"
            Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            id: sweep
            visible: bar.indeterminate
            width: parent.width * 0.28
            height: parent.height
            radius: height / 2
            color: "#238aff"
            NumberAnimation on x {
                from: -sweep.width
                to: sweep.parent ? sweep.parent.width : 0
                duration: 1300
                loops: Animation.Infinite
                running: bar.indeterminate
            }
        }
    }
}
