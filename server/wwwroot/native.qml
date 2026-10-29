import QtQuick
import QmlBrowser.Native
import "shared"

Page {
    id: page

    property string title: "QML Browser — Drawn in C++"

    property bool clockSweep: true
    property bool waveRunning: true
    property bool juliaSpin: true
    property bool plasmaRunning: true
    property real orbit: 0

    heading: "Drawn in C++"
    subheading: "Every item on this page is a C++ class compiled into QmlRenderer and " +
                "registered as import QmlBrowser.Native. QML sets their properties and " +
                "binds to what they report; the drawing itself happens in C++. Each card " +
                "uses a different way to get pixels from C++ into the scene graph."

    // Walks c around a circle where the set is at its most intricate.
    NumberAnimation on orbit {
        from: 0
        to: 2 * Math.PI
        duration: 30000
        loops: Animation.Infinite
        running: page.juliaSpin
    }
    onOrbitChanged: {
        if (!juliaSpin)
            return
        julia.cx = 0.7885 * Math.cos(orbit)
        julia.cy = 0.7885 * Math.sin(orbit)
    }

    Card {
        title: "QPainter: ClockFace"

        Text {
            width: parent.width
            text: "A QQuickPaintedItem. paint() gets an ordinary QPainter (gradients, pens, " +
                  "text) and the item is shown as a texture. A QTimer in C++ drives it; the " +
                  "digital readout below is a Text bound to the C++ timeText property. " +
                  "Source: renderer/native/ClockFace.cpp."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Row {
            spacing: 36

            Repeater {
                model: [
                    { label: "Local", offset: 0, accent: "#8ab4ff" },
                    // getTimezoneOffset() is UTC minus local, which is the
                    // shift from local time to UTC.
                    { label: "UTC", offset: new Date().getTimezoneOffset(), accent: "#f0b775" }
                ]

                Column {
                    required property var modelData
                    spacing: 8

                    ClockFace {
                        id: clock
                        width: 200
                        height: 200
                        accent: modelData.accent
                        offsetMinutes: modelData.offset
                        sweep: page.clockSweep
                    }
                    Text {
                        width: clock.width
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.label + "  " + clock.timeText
                        color: "#d7deee"
                        font.pixelSize: 15
                        font.family: "Consolas"
                    }
                }
            }
        }

        Row {
            spacing: 10
            PillButton {
                text: page.clockSweep ? "Tick" : "Sweep"
                onClicked: page.clockSweep = !page.clockSweep
            }
        }
    }

    Card {
        title: "Scene graph geometry: WaveShape"
        accent: "#7ce3b0"

        Text {
            width: parent.width
            text: "A QQuickItem that builds QSGGeometry in updatePaintNode(): a triangle " +
                  "strip with per-vertex colours for the fill, and indexed triangles with a " +
                  "feathered edge for the line. Nothing is rasterized on the CPU; QML " +
                  "animates phase and only the vertices change. Hover it: the marker is " +
                  "placed by calling the C++ method valueAt(x). " +
                  "Source: renderer/native/WaveShape.cpp."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: 200
            radius: 10
            color: "#0b0d14"
            border.color: "#232939"
            clip: true

            WaveShape {
                id: wave
                anchors.fill: parent
                anchors.margins: 1
                amplitude: 0.7
                frequency: 2.5
                lineWidth: 3

                NumberAnimation on phase {
                    from: 0
                    to: 2 * Math.PI
                    duration: 4000
                    loops: Animation.Infinite
                    running: page.waveRunning
                }

                HoverHandler {
                    id: waveHover
                }

                Rectangle {
                    readonly property real px: waveHover.point.position.x

                    visible: waveHover.hovered
                    width: 12
                    height: 12
                    radius: 6
                    color: "#0b0d14"
                    border.color: "#ffffff"
                    border.width: 2
                    x: px - width / 2
                    y: {
                        // valueAt() is C++, so the engine cannot see what it
                        // reads. Touching these properties re-runs the binding
                        // whenever the curve changes.
                        wave.phase; wave.amplitude; wave.frequency; wave.height
                        return wave.valueAt(px) - height / 2
                    }
                }
            }
        }

        Row {
            spacing: 10
            PillButton {
                text: page.waveRunning ? "Pause" : "Play"
                onClicked: page.waveRunning = !page.waveRunning
            }
            PillButton {
                text: "Calmer"
                onClicked: wave.amplitude = Math.max(0.1, wave.amplitude - 0.15)
            }
            PillButton {
                text: "Wilder"
                onClicked: wave.amplitude = Math.min(1.0, wave.amplitude + 0.15)
            }
            PillButton {
                text: "Fewer waves"
                onClicked: wave.frequency = Math.max(0.5, wave.frequency - 0.5)
            }
            PillButton {
                text: "More waves"
                onClicked: wave.frequency = Math.min(8, wave.frequency + 0.5)
            }
            PillButton {
                text: "Colour"
                property int next: 0
                onClicked: {
                    const tints = ["#8ab4ff", "#f0b775", "#c9a2ff", "#ff8095", "#7ce3b0"]
                    const c = tints[next++ % tints.length]
                    wave.lineColor = c
                    // #AARRGGBB: the same colour, translucent and clear.
                    wave.fillTop = "#8c" + c.substring(1)
                    wave.fillBottom = "#00" + c.substring(1)
                }
            }
        }
    }

    Card {
        title: "A texture computed in C++: JuliaSet"
        accent: "#c9a2ff"

        Text {
            width: parent.width
            text: "Plain C++ loops fill a QImage with the Julia set for z² + c, and " +
                  "updatePaintNode() uploads it as a texture. The image is rebuilt in " +
                  "updatePolish() only when c, the iteration limit or the size changes. " +
                  "Drag across it to choose c. Source: renderer/native/JuliaSet.cpp."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: 320
            radius: 10
            color: "#0b0d14"
            border.color: "#232939"
            clip: true

            JuliaSet {
                id: julia
                anchors.fill: parent
                anchors.margins: 1
                maxIterations: 120
            }

            MouseArea {
                anchors.fill: julia
                cursorShape: Qt.CrossCursor
                // Keeps the page's Flickable from taking a vertical drag.
                preventStealing: true

                function pick(x, y) {
                    page.juliaSpin = false
                    const c = julia.toComplex(x, y)
                    julia.cx = c.x
                    julia.cy = c.y
                }

                onPressed: (mouse) => pick(mouse.x, mouse.y)
                onPositionChanged: (mouse) => { if (pressed) pick(mouse.x, mouse.y) }
            }

            Rectangle {
                x: 12
                y: 12
                width: readout.implicitWidth + 20
                height: readout.implicitHeight + 12
                radius: 8
                color: "#cc0b0d14"

                Text {
                    id: readout
                    anchors.centerIn: parent
                    text: "c = " + julia.cx.toFixed(3) + (julia.cy < 0 ? " − " : " + ")
                          + Math.abs(julia.cy).toFixed(3) + "i   ·   "
                          + julia.maxIterations + " iterations   ·   "
                          + julia.renderTime.toFixed(1) + " ms"
                    color: "#d7deee"
                    font.pixelSize: 13
                    font.family: "Consolas"
                }
            }
        }

        Row {
            spacing: 10
            PillButton {
                text: page.juliaSpin ? "Stop" : "Orbit"
                onClicked: page.juliaSpin = !page.juliaSpin
            }
            PillButton {
                text: "Fewer iterations"
                onClicked: julia.maxIterations = Math.max(20, julia.maxIterations - 40)
            }
            PillButton {
                text: "More iterations"
                onClicked: julia.maxIterations = Math.min(600, julia.maxIterations + 40)
            }
            PillButton {
                text: "Hue"
                onClicked: julia.hue = (julia.hue + 0.17) % 1
            }
        }
    }

    Card {
        title: "A plain C library: NativeCanvas"
        accent: "#7ce3b0"

        Text {
            width: parent.width
            text: "The pixels here come from canvas/plasma.dll, a C library with no Qt in " +
                  "it. NativeCanvas loads it from the canvas folder next to QmlRenderer and " +
                  "calls qmlbrowser_canvas_draw() each frame; QML only binds time. The page " +
                  "names the library, never a path. Source: canvas/plasma/plasma.c."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: 240
            radius: 10
            color: "#0b0d14"
            border.color: "#232939"
            clip: true

            NativeCanvas {
                id: plasma
                anchors.fill: parent
                anchors.margins: 1
                library: "plasma"

                NumberAnimation on time {
                    from: 0
                    to: 600
                    duration: 600000
                    loops: Animation.Infinite
                    running: page.plasmaRunning
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                visible: plasma.status === NativeCanvas.Error
                text: plasma.errorString
                color: "#f0b775"
                font.pixelSize: 15
                font.family: "Segoe UI"
                wrapMode: Text.WordWrap
            }

            Rectangle {
                x: 12
                y: 12
                visible: plasma.status === NativeCanvas.Ready
                width: plasmaInfo.implicitWidth + 20
                height: plasmaInfo.implicitHeight + 12
                radius: 8
                color: "#cc0b0d14"

                Text {
                    id: plasmaInfo
                    anchors.centerIn: parent
                    text: "plasma · t = " + plasma.time.toFixed(1) + " · "
                          + plasma.renderTime.toFixed(1) + " ms"
                    color: "#d7deee"
                    font.pixelSize: 13
                    font.family: "Consolas"
                }
            }
        }

        Row {
            spacing: 10
            PillButton {
                text: page.plasmaRunning ? "Pause" : "Play"
                onClicked: page.plasmaRunning = !page.plasmaRunning
            }
        }
    }

    Card {
        title: "Adding your own"
        accent: "#f0b775"

        Text {
            width: parent.width
            text: "Write a QQuickItem or QQuickPaintedItem subclass, add it to " +
                  "renderer/native/, register it in NativeItems.cpp and rebuild QmlRenderer. " +
                  "A page can only import types the renderer was built with, and every " +
                  "site the browser opens can use them, so expose drawing, not file or " +
                  "process access."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

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
                text: '// renderer/native/NativeItems.cpp\n' +
                      'qmlRegisterType<ClockFace>("QmlBrowser.Native", 1, 0, "ClockFace");\n' +
                      '\n' +
                      '// any page\n' +
                      'import QmlBrowser.Native\n' +
                      '\n' +
                      'ClockFace { width: 200; height: 200; accent: "#8ab4ff" }'
            }
        }

        Row {
            spacing: 22
            NavLink { text: "3D in a page →"; url: "3d.qml"; color: "#f0b775" }
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
