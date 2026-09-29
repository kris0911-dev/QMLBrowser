import QtQuick
import "shared"

Page {
    id: page

    property string title: "QML Browser — Gallery"

    heading: "Rendering check"
    subheading: "Animations, gradients, Canvas painting and image loading — all driven by the " +
                "Qt Quick scene graph in the client, from a document that was downloaded a " +
                "moment ago."

    Card {
        title: "Animated transforms"

        Row {
            width: parent.width
            height: 150
            spacing: 24

            Item {
                width: 150; height: 150

                Rectangle {
                    anchors.centerIn: parent
                    width: 96; height: 96
                    radius: 16
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#7aa2ff" }
                        GradientStop { position: 1.0; color: "#c46bff" }
                    }
                    RotationAnimation on rotation {
                        from: 0; to: 360; duration: 6000
                        loops: Animation.Infinite; running: true
                    }
                }
            }

            Item {
                width: 150; height: 150

                Rectangle {
                    id: pulse
                    anchors.centerIn: parent
                    width: 96; height: 96
                    radius: width / 2
                    color: "#7ce3b0"

                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        running: true
                        NumberAnimation { from: 0.55; to: 1.0; duration: 900
                                          easing.type: Easing.InOutQuad }
                        NumberAnimation { from: 1.0; to: 0.55; duration: 900
                                          easing.type: Easing.InOutQuad }
                    }
                }
            }

            Item {
                width: 150; height: 150

                Repeater {
                    model: 12

                    Rectangle {
                        required property int index

                        width: 10; height: 10; radius: 5
                        color: "#f0b775"
                        opacity: 1.0 - index / 14
                        x: 70 + Math.cos(index * Math.PI / 6 + spin.angle) * 52
                        y: 70 + Math.sin(index * Math.PI / 6 + spin.angle) * 52
                    }
                }

                QtObject {
                    id: spin
                    property real angle: 0
                }

                NumberAnimation {
                    target: spin
                    property: "angle"
                    from: 0; to: 2 * Math.PI
                    duration: 2500
                    loops: Animation.Infinite
                    running: true
                }
            }
        }
    }

    Card {
        title: "Canvas painting"
        accent: "#7ce3b0"

        Canvas {
            id: wave
            width: parent.width
            height: 160
            renderStrategy: Canvas.Cooperative

            property real t: 0
            onTChanged: requestPaint()

            NumberAnimation on t {
                from: 0; to: 2 * Math.PI
                duration: 4000
                loops: Animation.Infinite
                running: true
            }

            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();

                for (let layer = 0; layer < 3; ++layer) {
                    ctx.beginPath();
                    ctx.moveTo(0, height);
                    for (let x = 0; x <= width; x += 6) {
                        const y = height / 2
                                + Math.sin(x / 70 + t + layer * 0.9) * (18 + layer * 10)
                                + Math.sin(x / 23 - t * 1.6) * 5;
                        ctx.lineTo(x, y);
                    }
                    ctx.lineTo(width, height);
                    ctx.closePath();
                    ctx.fillStyle = Qt.rgba(0.36 + layer * 0.07, 0.78 - layer * 0.1,
                                            0.62 + layer * 0.05, 0.28);
                    ctx.fill();
                }
            }
        }
    }

    Card {
        title: "Images fetched from the same server"
        accent: "#f0b775"

        Text {
            width: parent.width
            text: "The SVG below is referenced as a relative path. The Qt Quick engine resolves " +
                  "it against the document URL and downloads it over the same HTTP connection."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Row {
            spacing: 16

            Image {
                id: logo
                source: "assets/logo.svg"
                sourceSize.width: 120
                sourceSize.height: 120
                width: 120
                height: 120
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    text: "assets/logo.svg"
                    color: "#d7deee"; font.pixelSize: 15; font.family: "Consolas"
                }
                Text {
                    text: "status: " + [ "Null", "Ready", "Loading", "Error" ][logo.status]
                    color: "#7d8ba6"; font.pixelSize: 14; font.family: "Segoe UI"
                }
            }
        }
    }

    Row {
        spacing: 22
        NavLink { text: "← Home"; url: "index.qml" }
        NavLink { text: "About →"; url: "about.qml"; color: "#8ab4ff" }
    }
}
