import QtQuick

Rectangle {
    id: root

    property string glyph: ""
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
        anchors.centerIn: parent
        text: root.glyph
        color: root.enabled ? root.glyphColor : "#464d61"
        font.pixelSize: 15
        font.family: "Segoe UI Symbol"
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
