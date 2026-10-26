import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string label
    property real value: 0
    property real minimum: 0
    property real maximum: 1
    property real step: 0.01
    property int decimals: 2
    signal edited(real value)
    spacing: 2
    RowLayout {
        Layout.fillWidth: true
        Label { text: root.label; Layout.fillWidth: true }
        Label { text: root.value.toFixed(root.decimals); color: "#9eacc0"; font.family: "monospace" }
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.minimum
        to: root.maximum
        stepSize: root.step
        onMoved: root.edited(value)
        Accessible.name: root.label
        Binding { target: slider; property: "value"; value: root.value; when: !slider.pressed }
    }
}
