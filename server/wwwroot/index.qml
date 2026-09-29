import QtQuick
import "shared"

Page {
    id: page

    // The client reads `title` and puts it in the window title bar.
    property string title: "QML Browser — Home"

    property int clicks: 0
    property real phase: 0
    property real spread: 0.45

    heading: "It's QML, served over HTTP."
    subheading: "This page lives in server/wwwroot/index.qml. The browser fetched it as " +
                "source text and handed it to the Qt Quick engine — no HTML, no Chromium, " +
                "no web view anywhere in the stack."

    NumberAnimation on phase {
        from: 0
        to: 2 * Math.PI
        duration: 5000
        loops: Animation.Infinite
        running: true
    }

    Row {
        id: tiles
        width: parent.width
        spacing: 16

        Repeater {
            model: [
                { name: "About",   url: "about.qml",   desc: "How the two halves fit together", tint: "#8ab4ff" },
                { name: "Gallery", url: "gallery.qml", desc: "Animation and live rendering",    tint: "#7ce3b0" },
                { name: "Docs",    url: "docs.qml",    desc: "The page-facing browser API",     tint: "#f0b775" }
            ]

            Rectangle {
                required property var modelData

                width: (tiles.width - 2 * tiles.spacing) / 3
                height: 132
                radius: 14
                color: tile.containsMouse ? "#1d2534" : "#161a24"
                border.color: tile.containsMouse ? modelData.tint : "#232939"
                border.width: 1

                Behavior on color { ColorAnimation { duration: 140 } }

                Column {
                    x: 20
                    y: 24
                    width: parent.width - 40
                    spacing: 8

                    Text {
                        text: modelData.name
                        color: modelData.tint
                        font.pixelSize: 21
                        font.bold: true
                        font.family: "Segoe UI"
                    }
                    Text {
                        width: parent.width
                        text: modelData.desc
                        color: "#8d97ac"
                        font.pixelSize: 14
                        font.family: "Segoe UI"
                        wrapMode: Text.WordWrap
                    }
                }

                MouseArea {
                    id: tile
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: browser.navigate(modelData.url)
                }
            }
        }
    }

    Card {
        title: "A live engine, not a screenshot"

        Text {
            width: parent.width
            text: "Everything below runs real QML bindings and JavaScript inside the client " +
                  "process. Press the button or drag the slider and watch the bindings react."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Item { width: 1; height: 4 }

        Row {
            spacing: 18

            Rectangle {
                width: 124
                height: 40
                radius: 10
                color: press.pressed ? "#3f6bd8" : (press.containsMouse ? "#33509f" : "#26304a")
                border.color: "#43518f"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "Clicked " + page.clicks + "×"
                    color: "#eaf0ff"
                    font.pixelSize: 14
                    font.family: "Segoe UI"
                }

                MouseArea {
                    id: press
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.clicks++
                }
            }

            Item {
                id: slider
                width: 220
                height: 40

                readonly property real trackWidth: width - handle.width

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 6
                    radius: 3
                    color: "#26304a"

                    Rectangle {
                        width: handle.x + handle.width / 2
                        height: parent.height
                        radius: 3
                        color: "#7ce3b0"
                    }
                }

                Rectangle {
                    id: handle
                    width: 20
                    height: 20
                    radius: 10
                    x: page.spread * slider.trackWidth
                    y: (parent.height - height) / 2
                    color: "#ddffed"
                    border.color: "#7ce3b0"
                    border.width: 2

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.SizeHorCursor
                        drag.target: handle
                        drag.axis: Drag.XAxis
                        drag.minimumX: 0
                        drag.maximumX: slider.trackWidth
                        // Dragging replaces the x binding, so push the value back
                        // into `spread`, which is what the orbit below reads.
                        onPositionChanged: page.spread = handle.x / slider.trackWidth
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(page.spread * 100) + "%"
                color: "#7ce3b0"
                font.pixelSize: 15
                font.family: "Segoe UI"
            }
        }

        Item { width: 1; height: 6 }

        Item {
            id: orbit
            width: parent.width
            height: 110

            Repeater {
                model: 9

                Rectangle {
                    required property int index

                    width: 14
                    height: 14
                    radius: 7
                    color: Qt.hsla(0.33 + index * 0.035, 0.62, 0.64, 1.0)
                    x: orbit.width / 2 - width / 2
                        + Math.cos(page.phase + index * 0.52) * (36 + page.spread * 150)
                    y: orbit.height / 2 - height / 2
                        + Math.sin(page.phase + index * 0.52) * (18 + page.spread * 60)
                }
            }
        }
    }

    Card {
        title: "Try the address bar"
        accent: "#f0b775"

        Text {
            width: parent.width
            text: "Type any path under the document root, for example /gallery.qml, or /shared/ " +
                  "to get an auto-generated directory listing. A missing document returns a 404 " +
                  "page that is itself QML."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Row {
            spacing: 22
            NavLink { text: "/gallery.qml"; url: "gallery.qml" }
            NavLink { text: "/shared/"; url: "shared/"; color: "#f0b775" }
            NavLink { text: "/nope.qml (404)"; url: "nope.qml"; color: "#ff8095" }
        }
    }
}
