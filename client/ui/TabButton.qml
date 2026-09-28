import QtQuick
import QmlBrowser 1.0

// One entry in the tab strip.
Rectangle {
    id: root

    property var tab: null
    property bool active: false

    signal selected()
    signal closeRequested()

    height: 34
    radius: 9
    color: active ? "#222a3a" : (area.containsMouse ? "#181d29" : "transparent")
    border.color: active ? "#313c54" : "transparent"
    border.width: 1

    Behavior on color { ColorAnimation { duration: 110 } }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.MiddleButton)
                root.closeRequested();
            else
                root.selected();
        }
    }

    // Status dot: spins while loading, red when the renderer died.
    Item {
        id: indicator
        width: 12
        height: 12
        anchors.left: parent.left
        anchors.leftMargin: 11
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: {
                if (!root.tab) return "#4a5468";
                if (root.tab.crashed) return "#ff6b81";
                if (root.tab.status === BrowserTab.Loading) return "#4d8dff";
                if (root.tab.status === BrowserTab.Error) return "#f0b775";
                return "#5d9e7a";
            }

            SequentialAnimation on opacity {
                running: root.tab && root.tab.status === BrowserTab.Loading && !root.tab.crashed
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.25; duration: 500 }
                NumberAnimation { from: 0.25; to: 1.0; duration: 500 }
            }
        }
    }

    Text {
        anchors.left: indicator.right
        anchors.leftMargin: 9
        anchors.right: closeButton.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: root.tab ? root.tab.title : ""
        color: root.active ? "#eaf0fb" : "#98a2b8"
        font.pixelSize: 13
        font.family: "Segoe UI"
        elide: Text.ElideRight
    }

    Rectangle {
        id: closeButton
        width: 20
        height: 20
        radius: 5
        anchors.right: parent.right
        anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
        color: close.containsMouse ? "#3a4459" : "transparent"

        Text {
            anchors.centerIn: parent
            text: "\u2715"
            color: close.containsMouse ? "#ffffff" : "#77839b"
            font.pixelSize: 10
            font.family: "Segoe UI Symbol"
        }

        MouseArea {
            id: close
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.closeRequested()
        }
    }
}
