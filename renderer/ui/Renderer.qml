import QtQuick
import QmlRenderer 1.0

// Everything inside one tab. The browser process owns the chrome around it.
// This scene is drawn offscreen: the browser window composites the frame, so
// there is no renderer window to place or reparent.
Rectangle {
    id: root
    color: "#0e1017"

    // Set by the browser process. The reminder is drawn into the page frame,
    // which the browser composites, so it stays above the document.
    property string notice: ""

    onNoticeChanged: if (notice.length > 0) noticeTimer.restart()

    PageView {
        id: page
        objectName: "pageView"
        anchors.fill: parent
    }

    // Spinner while the document is in flight.
    Item {
        anchors.centerIn: parent
        width: 48
        height: 48
        visible: page.status === PageView.Loading

        Repeater {
            model: 8

            Rectangle {
                required property int index

                width: 7
                height: 7
                radius: 3.5
                color: "#4d8dff"
                opacity: 0.25 + 0.75 * ((index + spinner.step) % 8) / 8
                x: 24 - 3.5 + Math.cos(index * Math.PI / 4) * 18
                y: 24 - 3.5 + Math.sin(index * Math.PI / 4) * 18
            }
        }

        QtObject {
            id: spinner
            property int step: 0
        }

        Timer {
            interval: 90
            running: page.status === PageView.Loading
            repeat: true
            onTriggered: spinner.step = (spinner.step + 1) % 8
        }
    }

    // Error page
    Rectangle {
        anchors.fill: parent
        color: "#0e1017"
        visible: page.status === PageView.Error

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 96, 760)
            spacing: 18

            Text {
                text: "\u26A0  This page could not be displayed"
                color: "#ff8b98"
                font.pixelSize: 26
                font.bold: true
                font.family: "Segoe UI"
            }

            Text {
                width: parent.width
                text: page.url
                color: "#6f7a91"
                font.pixelSize: 14
                font.family: "Consolas"
                elide: Text.ElideMiddle
            }

            Rectangle {
                width: parent.width
                height: Math.min(detail.implicitHeight + 28, 260)
                radius: 10
                color: "#15171f"
                border.color: "#2a2f3d"
                border.width: 1

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 14
                    contentHeight: detail.implicitHeight
                    clip: true

                    Text {
                        id: detail
                        width: parent.width
                        text: page.errorString
                        color: "#c3cbdc"
                        font.pixelSize: 13
                        font.family: "Consolas"
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                }
            }

            Row {
                spacing: 12

                Rectangle {
                    width: 104
                    height: 36
                    radius: 9
                    color: retry.containsMouse ? "#2f4a8f" : "#25304a"
                    border.color: "#3c4b74"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Try again"
                        color: "#e8eeff"
                        font.pixelSize: 14
                        font.family: "Segoe UI"
                    }

                    MouseArea {
                        id: retry
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.reload()
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Is QmlServer.exe running?"
                    color: "#5f6a80"
                    font.pixelSize: 13
                    font.family: "Segoe UI"
                }
            }
        }
    }

    // Transient message from the browser, on top of everything else.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: opacity > 0.5 ? 26 : 6
        width: noticeLabel.implicitWidth + 40
        height: 42
        radius: 21
        color: "#1b2030"
        border.color: "#333d54"
        border.width: 1
        opacity: root.notice.length > 0 ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        Text {
            id: noticeLabel
            anchors.centerIn: parent
            text: root.notice
            color: "#dbe3f4"
            font.pixelSize: 14
            font.family: "Segoe UI"
        }
    }

    Timer {
        id: noticeTimer
        interval: 3200
        onTriggered: root.notice = ""
    }
}
