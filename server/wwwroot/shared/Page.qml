import QtQuick

// Scrollable page shell shared by every document on this site. Anything
// declared inside a Page is laid out in the centred content column.
Rectangle {
    id: root

    property string heading: ""
    property string subheading: ""
    default property alias content: column.data

    color: "#0e1017"

    // Decorative glow behind the header.
    Rectangle {
        width: parent.width * 1.4
        height: 420
        anchors.horizontalCenter: parent.horizontalCenter
        y: -200
        radius: height / 2
        opacity: 0.4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2d3f7d" }
            GradientStop { position: 1.0; color: "#0e1017" }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: layout.height + 112
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: layout
            x: Math.max(48, (flick.width - 880) / 2)
            y: 56
            width: Math.min(880, flick.width - 96)
            spacing: 26

            Column {
                width: parent.width
                spacing: 10
                visible: root.heading.length > 0

                Text {
                    text: root.heading
                    color: "#f4f7ff"
                    font.pixelSize: 40
                    font.bold: true
                    font.family: "Segoe UI"
                }
                Text {
                    width: parent.width
                    text: root.subheading
                    color: "#98a2b8"
                    font.pixelSize: 17
                    font.family: "Segoe UI"
                    wrapMode: Text.WordWrap
                    visible: text.length > 0
                }
            }

            Column {
                id: column
                width: parent.width
                spacing: 18
            }
        }
    }

    // Slim scroll indicator.
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: 4
        y: flick.visibleArea.yPosition * root.height
        width: 4
        radius: 2
        height: flick.visibleArea.heightRatio * root.height
        color: "#414b63"
        opacity: flick.moving ? 0.9 : 0.35
        visible: flick.contentHeight > flick.height
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
