import QtQuick

Rectangle {
    id: root

    property string glyph: ""
    // "fullscreen" draws the mark itself. The unicode glyph needs a font this
    // machine does not have, and the fallback shape does not fit the button.
    property string mark: ""
    property string tooltip: ""
    property color glyphColor: "#d5dcec"
    // Hover text goes to the status line: a popup here would be painted under
    // the renderer's native child window.
    readonly property alias hovered: area.containsMouse

    signal clicked()

    width: 36
    height: 32
    radius: 8
    color: !enabled ? "transparent"
                    : (area.pressed ? "#2b3450" : (area.containsMouse ? "#1e2534" : "transparent"))

    Behavior on color { ColorAnimation { duration: 90 } }

    Text {
        visible: root.mark === ""
        anchors.centerIn: parent
        text: root.glyph
        color: root.enabled ? root.glyphColor : "#464d61"
        font.pixelSize: 15
        font.family: "Segoe UI Symbol"
    }

    // Four corners, the usual enter-full-screen mark.
    Item {
        visible: root.mark === "fullscreen"
        anchors.centerIn: parent
        width: 12
        height: 12

        readonly property color ink: root.enabled ? root.glyphColor : "#464d61"

        Rectangle { x: 0;  y: 0;  width: 4; height: 1.5; color: parent.ink }
        Rectangle { x: 0;  y: 0;  width: 1.5; height: 4; color: parent.ink }
        Rectangle { x: 8;  y: 0;  width: 4; height: 1.5; color: parent.ink }
        Rectangle { x: 10.5; y: 0; width: 1.5; height: 4; color: parent.ink }
        Rectangle { x: 0;  y: 10.5; width: 4; height: 1.5; color: parent.ink }
        Rectangle { x: 0;  y: 8;  width: 1.5; height: 4; color: parent.ink }
        Rectangle { x: 8;  y: 10.5; width: 4; height: 1.5; color: parent.ink }
        Rectangle { x: 10.5; y: 8; width: 1.5; height: 4; color: parent.ink }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
