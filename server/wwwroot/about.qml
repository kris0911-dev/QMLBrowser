import QtQuick
import "shared"

Page {
    id: page

    property string title: "QML Browser — About"

    heading: "How the two halves talk"
    subheading: "A C++ HTTP server hands out .qml documents; a Qt Quick client fetches them " +
                "and instantiates them at runtime. Both sides are plain Qt — nothing else."

    Card {
        title: "Request flow"

        Row {
            width: parent.width
            spacing: 12

            Rectangle {
                width: (parent.width - 2 * parent.spacing - 150) / 2
                height: 130
                radius: 12
                color: "#16233a"
                border.color: "#3a5c96"
                border.width: 1

                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "QmlServer.exe"; color: "#8ab4ff"; font.pixelSize: 17
                           font.bold: true; font.family: "Segoe UI" }
                    Text { text: "QTcpServer"; color: "#7d8ba6"; font.pixelSize: 13
                           font.family: "Segoe UI" }
                    Text { text: "wwwroot/*.qml"; color: "#7d8ba6"; font.pixelSize: 13
                           font.family: "Segoe UI" }
                }
            }

            Column {
                width: 150
                height: 130
                spacing: 10

                Item { width: 1; height: 26 }

                Column {
                    width: parent.width
                    spacing: 2
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "GET /index.qml"
                        color: "#9aa5bb"; font.pixelSize: 12; font.family: "Consolas"
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "──────────▶"
                        color: "#5c6a86"; font.pixelSize: 14; font.family: "Consolas"
                    }
                }

                Column {
                    width: parent.width
                    spacing: 2
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "◀──────────"
                        color: "#5c6a86"; font.pixelSize: 14; font.family: "Consolas"
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "200 text/x-qml"
                        color: "#9aa5bb"; font.pixelSize: 12; font.family: "Consolas"
                    }
                }
            }

            Rectangle {
                width: (parent.width - 2 * parent.spacing - 150) / 2
                height: 130
                radius: 12
                color: "#16302a"
                border.color: "#3a8f6d"
                border.width: 1

                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: "QmlBrowser.exe"; color: "#7ce3b0"; font.pixelSize: 17
                           font.bold: true; font.family: "Segoe UI" }
                    Text { text: "QQmlComponent"; color: "#7d8ba6"; font.pixelSize: 13
                           font.family: "Segoe UI" }
                    Text { text: "Qt Quick scene"; color: "#7d8ba6"; font.pixelSize: 13
                           font.family: "Segoe UI" }
                }
            }
        }
    }

    Card {
        title: "What the client does with the bytes"
        accent: "#7ce3b0"

        Repeater {
            model: [
                "PageView (a QQuickItem written in C++) issues the GET through QNetworkAccessManager.",
                "The response body is passed to QQmlComponent::setData together with the request URL, " +
                "so relative paths inside the document resolve back to the server.",
                "The component is instantiated into a fresh QQmlContext that exposes the `browser` object.",
                "The resulting QQuickItem is reparented into the viewport and sized to fill it.",
                "Compile or runtime errors are collected from QQmlComponent::errors() and shown as an error page."
            ]

            Row {
                required property var modelData
                required property int index

                width: parent.width
                spacing: 12

                Rectangle {
                    width: 24; height: 24; radius: 12
                    color: "#1d3a2f"
                    border.color: "#3a8f6d"
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: index + 1
                        color: "#7ce3b0"; font.pixelSize: 13; font.family: "Segoe UI"
                    }
                }

                Text {
                    width: parent.width - 36
                    text: modelData
                    color: "#98a2b8"
                    font.pixelSize: 15
                    font.family: "Segoe UI"
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Card {
        title: "Deliberately not a web browser"
        accent: "#f0b775"

        Text {
            width: parent.width
            text: "There is no HTML parser, no CSS engine, no JavaScript DOM and no Chromium. " +
                  "The transport is HTTP because it is convenient and debuggable, but the " +
                  "document format is QML and the renderer is the Qt Quick scene graph talking " +
                  "to Direct3D through Qt RHI."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }
    }

    Row {
        spacing: 22
        NavLink { text: "← Home"; url: "index.qml" }
        NavLink { text: "Docs →"; url: "docs.qml"; color: "#f0b775" }
    }
}
