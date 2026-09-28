import QtQuick

// A surface panel with an optional title. Children go into the body column.
Rectangle {
    id: root

    property string title: ""
    property string accent: "#8ab4ff"
    default property alias body: body.data

    width: parent ? parent.width : 600
    height: inner.height + 44
    radius: 14
    color: "#161a24"
    border.color: "#232939"
    border.width: 1

    Rectangle {
        width: 3
        height: parent.height - 28
        x: 0
        y: 14
        radius: 2
        color: root.accent
        visible: root.title.length > 0
    }

    Column {
        id: inner
        x: 24
        y: 22
        width: parent.width - 48
        spacing: 12

        Text {
            text: root.title
            color: "#eef2fb"
            font.pixelSize: 20
            font.bold: true
            font.family: "Segoe UI"
            visible: text.length > 0
        }

        Column {
            id: body
            width: parent.width
            spacing: 10
        }
    }
}
