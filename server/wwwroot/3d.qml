import QtQuick
import "shared"

Page {
    id: page

    property string title: "QML Browser — 3D"

    heading: "3D in a page"
    subheading: "The renderer draws every page on the GPU through Qt RHI (Metal on macOS, " +
                "Direct3D 11 on Windows), so a document can carry a real 3D scene. Drag " +
                "either view to turn it."

    Card {
        title: "Qt Quick 3D"

        Text {
            width: parent.width
            text: "A View3D with physically based materials, a shadow-casting light and " +
                  "multisampling. The scene is a second document, scene3d/QuickScene.qml, " +
                  "loaded by a Loader: when the browser's Qt kit has no Qt Quick 3D, only " +
                  "this card says so and the rest of the page still works."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: 420
            radius: 10
            color: "#0b0d14"
            border.color: "#232939"
            clip: true

            Loader {
                id: scene
                anchors.fill: parent
                anchors.margins: 1
                source: "scene3d/QuickScene.qml"
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - 80
                spacing: 10
                visible: scene.status === Loader.Error

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: "Qt Quick 3D is not available"
                    color: "#f0b775"
                    font.pixelSize: 20
                    font.bold: true
                    font.family: "Segoe UI"
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: "The renderer could not import QtQuick3D. Install the Qt Quick 3D " +
                          "module for the Qt kit the browser is built with, then rebuild so " +
                          "it is deployed next to QmlRenderer. The pure Qt Quick cube below " +
                          "needs nothing extra."
                    color: "#98a2b8"
                    font.pixelSize: 15
                    font.family: "Segoe UI"
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                anchors.centerIn: parent
                visible: scene.status === Loader.Loading
                text: "Loading the scene…"
                color: "#98a2b8"
                font.pixelSize: 15
                font.family: "Segoe UI"
            }
        }

        Row {
            spacing: 10
            visible: scene.status === Loader.Ready

            PillButton {
                text: scene.item && scene.item.spinning ? "Pause" : "Spin"
                onClicked: scene.item.spinning = !scene.item.spinning
            }
            PillButton {
                text: "Closer"
                onClicked: scene.item.distance = Math.max(280, scene.item.distance - 100)
            }
            PillButton {
                text: "Farther"
                onClicked: scene.item.distance = Math.min(1200, scene.item.distance + 100)
            }
            PillButton {
                text: "Reset"
                onClicked: {
                    scene.item.yaw = -28
                    scene.item.pitch = -20
                    scene.item.distance = 520
                }
            }
        }
    }

    Card {
        title: "Plain Qt Quick, no 3D module"
        accent: "#7ce3b0"

        Text {
            width: parent.width
            text: "Six Rectangles, each placed by a Matrix4x4 transform with a perspective " +
                  "term. The scene graph draws them like any other item; faces turned away " +
                  "from the viewer are hidden. It works with every Qt kit."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Item {
            id: stage
            width: parent.width
            height: 340

            Item {
                id: cube

                property real size: 170
                property real yaw: 30
                property real pitch: -22
                property real distance: 700
                property bool spinning: !cubeDrag.active

                anchors.centerIn: parent
                width: size
                height: size

                NumberAnimation on yaw {
                    from: 0
                    to: 360
                    duration: 12000
                    loops: Animation.Infinite
                    running: cube.spinning
                }

                // The face's orientation, then the view rotation.
                function orientation(rx, ry, yaw, pitch) {
                    var m = Qt.matrix4x4()
                    m.rotate(pitch, Qt.vector3d(1, 0, 0))
                    m.rotate(yaw, Qt.vector3d(0, 1, 0))
                    m.rotate(ry, Qt.vector3d(0, 1, 0))
                    m.rotate(rx, Qt.vector3d(1, 0, 0))
                    return m
                }

                // Item coordinates of a face to the cube's coordinates, with
                // perspective: w = 1 - z / distance.
                function placement(rx, ry, yaw, pitch) {
                    var half = size / 2
                    var m = Qt.matrix4x4()
                    m.translate(Qt.vector3d(half, half, 0))
                    m = m.times(Qt.matrix4x4(1, 0, 0, 0,
                                             0, 1, 0, 0,
                                             0, 0, 1, 0,
                                             0, 0, -1 / distance, 1))
                    m = m.times(orientation(rx, ry, yaw, pitch))
                    m.translate(Qt.vector3d(-half, -half, half))
                    // Qt Quick's projection clips depth outside -1..1, and
                    // the depth is not needed once w holds the perspective:
                    // flatten it, as Rotation does for its own 3D turns.
                    m.m31 = 0
                    m.m32 = 0
                    m.m33 = 0
                    m.m34 = 0
                    return m
                }

                Repeater {
                    model: [
                        { rx: 0,   ry: 0,    top: "#4f8cff", bottom: "#2a4fa8", label: "Qt" },
                        { rx: 0,   ry: 180,  top: "#c46bff", bottom: "#6d3a9c", label: "QML" },
                        { rx: 0,   ry: 90,   top: "#7ce3b0", bottom: "#3a8a65", label: "RHI" },
                        { rx: 0,   ry: -90,  top: "#f0b775", bottom: "#a2702f", label: "3D" },
                        { rx: -90, ry: 0,    top: "#ff6b8b", bottom: "#a03550", label: "GPU" },
                        { rx: 90,  ry: 0,    top: "#8ab4ff", bottom: "#41618f", label: "UI" }
                    ]

                    Rectangle {
                        required property var modelData

                        width: cube.size
                        height: cube.size
                        antialiasing: true
                        border.color: Qt.lighter(modelData.top, 1.3)
                        border.width: 2
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: modelData.top }
                            GradientStop { position: 1.0; color: modelData.bottom }
                        }
                        // Facing away from the viewer: the z of the face normal,
                        // which is m33 of its orientation, is negative.
                        visible: cube.orientation(modelData.rx, modelData.ry,
                                                  cube.yaw, cube.pitch).m33 > 0.001
                        transform: Matrix4x4 {
                            matrix: cube.placement(modelData.rx, modelData.ry, cube.yaw, cube.pitch)
                        }

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: "#ffffff"
                            font.pixelSize: 44
                            font.bold: true
                            font.family: "Segoe UI"
                        }
                    }
                }
            }

            DragHandler {
                id: cubeDrag
                target: null
                grabPermissions: PointerHandler.TakeOverForbidden

                property real startYaw
                property real startPitch

                onActiveChanged: {
                    if (active) {
                        startYaw = cube.yaw
                        startPitch = cube.pitch
                    }
                }
                onActiveTranslationChanged: {
                    cube.yaw = startYaw + activeTranslation.x * 0.5
                    cube.pitch = Math.max(-85, Math.min(85, startPitch - activeTranslation.y * 0.5))
                }
            }
        }
    }

    Card {
        title: "3D in an app tab"
        accent: "#c9a2ff"

        Text {
            width: parent.width
            text: "The Qt Widgets app has a 3D page too: an OpenGL scene in a QOpenGLWidget. " +
                  "The app runs in QmlBrowserHost's GPU mode, so it has a real OpenGL " +
                  "context, and each frame is read back into the tab."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Row {
            spacing: 22
            NavLink { text: "Open the 3D page of the app →"; url: "app://widgets/3d"; color: "#c9a2ff" }
            NavLink { text: "Back to the start page"; url: "index.qml" }
        }
    }

    component PillButton: Rectangle {
        id: button

        property string text
        signal clicked()

        width: label.implicitWidth + 28
        height: 32
        radius: 16
        color: tap.pressed ? "#2a3350" : (hover.hovered ? "#232a3d" : "#1b2130")
        border.color: "#313a50"

        Text {
            id: label
            anchors.centerIn: parent
            text: button.text
            color: "#dbe2f0"
            font.pixelSize: 14
            font.family: "Segoe UI"
        }
        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            id: tap
            onTapped: button.clicked()
        }
    }
}
