import QtQuick
import QtQuick3D

// A Qt Quick 3D scene: physically based materials, a shadow-casting key
// light, a coloured fill light and MSAA. 3d.qml loads this file through a
// Loader, so the page still works with a Qt kit that has no Qt Quick 3D.
//
// The renderer draws it on its own graphics API (Metal on macOS, Direct3D 11
// on Windows) through QQuickRenderControl, like every other page.
Item {
    id: root

    property real yaw: -28
    property real pitch: -20
    property real distance: 520
    property bool spinning: true

    View3D {
        id: view
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#0b0d14"
            // Fades the floor into the background instead of showing its edge.
            fog: Fog {
                enabled: true
                color: "#0b0d14"
                depthEnabled: true
                depthNear: 500
                depthFar: 1600
            }
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            eulerRotation: Qt.vector3d(root.pitch, root.yaw, 0)
            y: -10

            PerspectiveCamera {
                position: Qt.vector3d(0, 0, root.distance)
                clipNear: 10
                clipFar: 5000
            }
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-52, -35, 0)
            brightness: 1.3
            castsShadow: true
            shadowMapQuality: Light.ShadowMapQualityHigh
            shadowFactor: 55
        }

        PointLight {
            position: Qt.vector3d(-260, 160, 220)
            color: "#8ab4ff"
            brightness: 1.6
            quadraticFade: 0.00002
        }

        // The floor.
        Model {
            source: "#Rectangle"
            y: -100
            eulerRotation.x: -90
            scale: Qt.vector3d(80, 80, 1)
            receivesShadows: true
            materials: PrincipledMaterial {
                baseColor: "#1b2130"
                roughness: 0.85
            }
        }

        Node {
            id: carousel

            NumberAnimation on eulerRotation.y {
                from: 0
                to: 360
                duration: 18000
                loops: Animation.Infinite
                running: root.spinning
            }

            Model {
                source: "#Sphere"
                position: Qt.vector3d(-150, 0, 0)
                castsShadows: true
                materials: PrincipledMaterial {
                    // Fully metallic would only reflect the (empty) sky.
                    baseColor: "#c9d2e3"
                    metalness: 0.55
                    roughness: 0.22
                }
            }

            Model {
                source: "#Cube"
                castsShadows: true
                scale: Qt.vector3d(1.1, 1.1, 1.1)
                materials: PrincipledMaterial {
                    baseColor: "#4f8cff"
                    roughness: 0.32
                }
                NumberAnimation on eulerRotation.x {
                    from: 0
                    to: 360
                    duration: 7000
                    loops: Animation.Infinite
                    running: root.spinning
                }
            }

            Model {
                source: "#Cylinder"
                position: Qt.vector3d(150, 0, 0)
                castsShadows: true
                materials: PrincipledMaterial {
                    baseColor: "#7ce3b0"
                    roughness: 0.45
                }
            }

            Model {
                source: "#Cone"
                position: Qt.vector3d(0, 0, -150)
                castsShadows: true
                materials: PrincipledMaterial {
                    baseColor: "#f0b775"
                    roughness: 0.5
                }
            }

            Model {
                source: "#Sphere"
                position: Qt.vector3d(0, -45, 150)
                scale: Qt.vector3d(0.55, 0.55, 0.55)
                castsShadows: true
                materials: PrincipledMaterial {
                    baseColor: "#ff6b8b"
                    roughness: 0.08
                    clearcoatAmount: 1.0
                }
            }
        }
    }

    // Drag to orbit. The page scrolls in a Flickable, which would otherwise
    // take the drag over once it passes the threshold.
    DragHandler {
        id: orbit
        target: null
        grabPermissions: PointerHandler.TakeOverForbidden

        property real startYaw
        property real startPitch

        onActiveChanged: {
            if (active) {
                startYaw = root.yaw
                startPitch = root.pitch
            }
        }
        onActiveTranslationChanged: {
            root.yaw = startYaw - activeTranslation.x * 0.4
            root.pitch = Math.max(-80, Math.min(8, startPitch - activeTranslation.y * 0.3))
        }
    }
}
