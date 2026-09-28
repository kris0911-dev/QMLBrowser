import QtQuick
import "shared"

Page {
    id: page

    property string title: "QML Browser — Docs"

    heading: "Writing pages for this browser"
    subheading: "A page is an ordinary .qml document whose root is an Item. Drop it anywhere " +
                "under server/wwwroot and it is immediately reachable by URL."

    Card {
        title: "The `browser` object"

        Text {
            width: parent.width
            text: "Every document is instantiated in a context that exposes a `browser` object. " +
                  "It is the C++ PageView instance, so these are real Qt properties and slots."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: [
                { sig: "browser.navigate(url)", desc: "Navigate to a relative or absolute URL and push a history entry." },
                { sig: "browser.reload()",      desc: "Re-fetch the current document, bypassing nothing — the server sends no-cache." },
                { sig: "browser.url",           desc: "The absolute URL of the document currently displayed." },
                { sig: "browser.httpStatus",    desc: "The status code the server returned for this document." }
            ]

            Column {
                required property var modelData

                width: parent.width
                spacing: 3

                Text {
                    text: modelData.sig
                    color: "#8ab4ff"
                    font.pixelSize: 15
                    font.family: "Consolas"
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
        }
    }

    Card {
        title: "Minimal page"
        accent: "#7ce3b0"

        Rectangle {
            width: parent.width
            height: code.height + 32
            radius: 10
            color: "#0b0d13"
            border.color: "#222838"
            border.width: 1

            Text {
                id: code
                x: 16
                y: 16
                width: parent.width - 32
                color: "#c9d4ea"
                font.pixelSize: 14
                font.family: "Consolas"
                text: 'import QtQuick\n' +
                      '\n' +
                      'Rectangle {\n' +
                      '    property string title: "Hello"   // shown in the window title\n' +
                      '    color: "#101218"\n' +
                      '\n' +
                      '    Text {\n' +
                      '        anchors.centerIn: parent\n' +
                      '        text: "Hello from " + browser.url\n' +
                      '        color: "white"\n' +
                      '    }\n' +
                      '\n' +
                      '    MouseArea {\n' +
                      '        anchors.fill: parent\n' +
                      '        onClicked: browser.navigate("index.qml")\n' +
                      '    }\n' +
                      '}'
            }
        }
    }

    Card {
        title: "Rules the engine enforces"
        accent: "#f0b775"

        Repeater {
            model: [
                "The root object must be an Item (Rectangle, Item, Page…). The browser sizes it to the viewport.",
                "Declare `property string title` to set the window title.",
                "Only modules shipped with the client can be imported — QtQuick works, custom C++ plugins do not.",
                "Relative imports work if the directory contains a qmldir file; see /shared/qmldir on this server.",
                "Remote documents are sandboxed by Qt: they cannot read local files off the machine running the client."
            ]

            Row {
                required property var modelData

                width: parent.width
                spacing: 10

                Text { text: "•"; color: "#f0b775"; font.pixelSize: 15 }
                Text {
                    width: parent.width - 22
                    text: modelData
                    color: "#98a2b8"
                    font.pixelSize: 15
                    font.family: "Segoe UI"
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Row {
        spacing: 22
        NavLink { text: "← Home"; url: "index.qml" }
        NavLink { text: "Gallery →"; url: "gallery.qml"; color: "#7ce3b0" }
    }
}
