import QtQuick

// A hyperlink. `url` is resolved relative to the document it appears in, so
// "about.qml" and "shared/../docs.qml" both work exactly like on the web.
Item {
    id: root

    property string text: ""
    property string url: ""
    property color color: "#8ab4ff"
    property int pixelSize: 16

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight

    Text {
        id: label
        text: root.text
        color: area.containsMouse ? Qt.lighter(root.color, 1.25) : root.color
        font.pixelSize: root.pixelSize
        font.family: "Segoe UI"
        font.underline: area.containsMouse
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: browser.navigate(root.url)
    }
}
