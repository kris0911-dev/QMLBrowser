import QtQuick
import QtQuick.Window
import QmlBrowser 1.0

Window {
    id: window

    width: 1280
    height: 860
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    color: "#0b0d13"
    title: (current && current.title.length > 0 ? current.title + " — " : "") + "QML Browser"

    readonly property var current: tabs.currentTab
    // Hover help, shown in the status line rather than as a popup.
    property string hint: ""

    readonly property bool fullScreen: tabs.fullScreen
    readonly property bool chromeHidden: !tabs.chromeVisible
    // Full screen hides the chrome too, but it is a separate choice.
    readonly property bool barsHidden: fullScreen || chromeHidden

    // What to go back to after full screen. Recorded as it happens rather than
    // read when F11 is pressed, because by then the window manager may already
    // have moved on and a maximised window would come back merely restored.
    property int windowedVisibility: Window.Windowed

    onVisibilityChanged: function (visibility) {
        if (visibility === Window.Windowed || visibility === Window.Maximized)
            windowedVisibility = visibility;
        Qt.callLater(updateCaptionDrag);
    }

    onFullScreenChanged: visibility = fullScreen ? Window.FullScreen : windowedVisibility

    TabManager {
        id: tabs
        homeUrl: initialUrl

        onLastTabClosed: window.close()
        onFocusAddressBarRequested: { input.forceActiveFocus(); input.selectAll(); }
        onToggleSourceRequested: sourcePanel.shown = !sourcePanel.shown
    }

    Component.onCompleted: {
        tabs.addTab(initialUrl);
        Qt.callLater(updateCaptionDrag);
    }

    // The empty part of the tab strip stands in for the title bar: Windows
    // treats it as a caption, so it drags the window and maximises on double click.
    function updateCaptionDrag() {
        var gap = captionButtons.x - (tabRow.x + tabRow.width);
        if (barsHidden || gap < 8 || tabStrip.height < 1) {
            tabs.setCaptionDragRegion(0, 0, 0, 0);
            return;
        }
        var ratio = Screen.devicePixelRatio;
        tabs.setCaptionDragRegion((tabRow.x + tabRow.width) * ratio, 0,
                                  gap * ratio, tabStrip.height * ratio);
    }

    onWidthChanged: Qt.callLater(updateCaptionDrag)
    onBarsHiddenChanged: Qt.callLater(updateCaptionDrag)

    // Drawn rather than taken from an icon font: Segoe MDL2 Assets paints a
    // solid black box behind every glyph under Qt Quick.
    component WindowButton: Rectangle {
        id: button

        property string kind: "min"   // min, max, restore, close
        property string tip: ""
        signal activated()

        readonly property bool danger: kind === "close"
        readonly property color ink: area.containsMouse && danger ? "#ffffff" : "#c5cedf"

        width: 46
        height: captionButtons.height
        color: !area.containsMouse ? "transparent"
             : danger ? "#c42b1c"
             : "#1b2230"

        Item {
            anchors.centerIn: parent
            width: 12
            height: 12

            Rectangle {
                visible: button.kind === "min"
                anchors.centerIn: parent
                width: 10
                height: 1
                color: button.ink
            }

            Rectangle {
                visible: button.kind === "max"
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 1
                color: "transparent"
                border.color: button.ink
                border.width: 1
            }

            Item {
                visible: button.kind === "restore"
                anchors.centerIn: parent
                width: 12
                height: 12

                Rectangle {
                    x: 3
                    y: 0
                    width: 8
                    height: 8
                    radius: 1
                    color: "transparent"
                    border.color: button.ink
                    border.width: 1
                }
                Rectangle {
                    x: 0
                    y: 3
                    width: 8
                    height: 8
                    radius: 1
                    color: area.containsMouse ? button.color : tabStrip.color
                    border.color: button.ink
                    border.width: 1
                }
            }

            Item {
                visible: button.kind === "close"
                anchors.centerIn: parent
                width: 10
                height: 10
                rotation: 45

                Rectangle {
                    anchors.centerIn: parent
                    width: 11
                    height: 1
                    color: button.ink
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 11
                    color: button.ink
                }
            }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onClicked: button.activated()
            onContainsMouseChanged: window.hint = containsMouse ? button.tip : ""
        }
    }

    // ------------------------------------------------------------- tab strip

    Rectangle {
        id: tabStrip
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: window.barsHidden ? 0 : 42
        visible: !window.barsHidden
        color: "#0e1119"

        Row {
            id: tabRow
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            onWidthChanged: Qt.callLater(window.updateCaptionDrag)

            Repeater {
                model: tabs.tabs

                TabButton {
                    required property var modelData
                    required property int index

                    tab: modelData
                    active: index === tabs.currentIndex
                    width: Math.max(96, Math.min(220,
                            (tabStrip.width - captionButtons.width - 52)
                                    / Math.max(1, tabs.count) - 4))

                    onSelected: tabs.currentIndex = index
                    onCloseRequested: tabs.closeTab(index)
                }
            }

            Rectangle {
                width: 30
                height: 30
                radius: 8
                anchors.verticalCenter: parent.verticalCenter
                color: plus.containsMouse ? "#1e2534" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: "#a9b4c9"
                    font.pixelSize: 18
                    font.family: "Segoe UI"
                }

                MouseArea {
                    id: plus
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tabs.addTab()
                    onContainsMouseChanged: window.hint = containsMouse ? "New tab  (Ctrl+T)" : ""
                }
            }
        }

        // Minimise, maximise/restore, close. They replace the Windows caption
        // buttons, so they sit flush in the corner the way Chrome's do.
        Row {
            id: captionButtons
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            z: 2

            WindowButton {
                kind: "min"
                tip: "Minimize"
                onActivated: window.showMinimized()
            }
            WindowButton {
                kind: window.visibility === Window.Maximized ? "restore" : "max"
                tip: window.visibility === Window.Maximized ? "Restore" : "Maximize"
                onActivated: window.visibility = window.visibility === Window.Maximized
                             ? Window.Windowed : Window.Maximized
            }
            WindowButton {
                kind: "close"
                tip: "Close"
                onActivated: window.close()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            z: 3
            color: "#1b2030"
        }
    }

    // --------------------------------------------------------------- toolbar

    Rectangle {
        id: toolbar
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: window.barsHidden ? 0 : 54
        visible: !window.barsHidden
        color: "#12151d"

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            ToolButton {
                glyph: "\u2190"
                tooltip: "Back  (Alt+Left)"
                enabled: window.current ? window.current.canGoBack : false
                onClicked: window.current.back()
                onHoveredChanged: window.hint = hovered ? tooltip : ""
            }
            ToolButton {
                glyph: "\u2192"
                tooltip: "Forward  (Alt+Right)"
                enabled: window.current ? window.current.canGoForward : false
                onClicked: window.current.forward()
                onHoveredChanged: window.hint = hovered ? tooltip : ""
            }
            ToolButton {
                readonly property bool busy:
                    window.current ? window.current.status === BrowserTab.Loading : false
                glyph: busy ? "\u2715" : "\u21BB"
                tooltip: busy ? "Stop" : "Reload  (F5)"
                enabled: window.current !== null
                onClicked: busy ? window.current.stop() : window.current.reload()
                onHoveredChanged: window.hint = hovered ? tooltip : ""
            }
            ToolButton {
                glyph: "\u2302"
                tooltip: "Home"
                enabled: window.current !== null
                onClicked: window.current.navigateToText(tabs.homeUrl)
                onHoveredChanged: window.hint = hovered ? tooltip : ""
            }
        }

        Rectangle {
            id: addressBar
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 176
            anchors.right: parent.right
            anchors.rightMargin: 180
            height: 36
            radius: 10
            color: input.activeFocus ? "#1a2030" : "#171b26"
            border.color: input.activeFocus ? "#4a6cc0" : "#262c3b"
            border.width: 1

            Text {
                id: scheme
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: window.current && window.current.status === BrowserTab.Error
                      ? "\u26A0" : "\u26AD"
                color: window.current && window.current.status === BrowserTab.Error
                       ? "#ff7a8a" : "#6f7a91"
                font.pixelSize: 14
                font.family: "Segoe UI Symbol"
            }

            TextInput {
                id: input
                anchors.left: scheme.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                color: "#e6ecf9"
                selectionColor: "#3a5da8"
                selectByMouse: true
                font.pixelSize: 14
                font.family: "Segoe UI"
                text: window.current ? window.current.url : ""

                onAccepted: {
                    // navigateToText resolves hosts ("127.0.0.1") to http and
                    // leaves document names ("about.qml") relative. Repeating
                    // the URL already open is a reload, not a second history entry.
                    if (window.current)
                        window.current.navigateToText(text);
                    focus = false;
                }

                // Keep showing the live URL unless the user is editing it.
                Connections {
                    target: tabs
                    function onCurrentChanged() {
                        // Switching tabs always shows that tab's address, including
                        // a new tab whose address is still empty.
                        input.text = window.current ? window.current.url : ""
                    }
                }
                Connections {
                    target: window.current
                    ignoreUnknownSignals: true
                    function onStateChanged() {
                        if (!input.activeFocus)
                            input.text = window.current ? window.current.url : "";
                    }
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            ToolButton {
                glyph: "\u2191"
                tooltip: "Hide the bars  (Ctrl+Shift+F)"
                onClicked: tabs.toggleChrome()
                onHoveredChanged: window.hint = hovered ? tooltip : ""
            }
            ToolButton {
                mark: "fullscreen"
                tooltip: "Full screen  (F11)"
                onClicked: tabs.toggleFullScreen()
                onHoveredChanged: window.hint = hovered ? tooltip : ""
            }
            ToolButton {
                glyph: "\u2263"
                tooltip: "View source  (Ctrl+U)"
                glyphColor: sourcePanel.shown ? "#8ab4ff" : "#d5dcec"
                onClicked: sourcePanel.shown = !sourcePanel.shown
                onHoveredChanged: window.hint = hovered ? tooltip : ""
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            height: 2
            width: parent.width * (window.current ? window.current.progress : 0)
            color: "#4d8dff"
            visible: window.current && window.current.status === BrowserTab.Loading
        }

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: "#1f2431"
        }
    }

    // --------------------------------------------------------------- content

    Item {
        id: content
        anchors.top: toolbar.bottom
        anchors.bottom: statusbar.top
        anchors.left: parent.left
        anchors.right: sourcePanel.left

        // Shown through while a new tab's renderer is still starting. The native
        // child covers it once that window exists; without this the hole is white.
        Rectangle {
            anchors.fill: parent
            color: "#0e1017"
        }

        // The active renderer's window is placed over this rectangle. Whatever
        // QML draws here is only visible when that tab has no live renderer.
        TabViewport {
            id: viewport
            manager: tabs
            anchors.fill: parent
        }

        Rectangle {
            anchors.fill: parent
            color: "#0e1017"
            visible: window.current ? window.current.crashed : false

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 96, 620)
                spacing: 16

                Text {
                    text: "\u2298  This tab has stopped"
                    color: "#ff8b98"
                    font.pixelSize: 26
                    font.bold: true
                    font.family: "Segoe UI"
                }

                Text {
                    width: parent.width
                    text: window.current ? window.current.errorString : ""
                    color: "#98a2b8"
                    font.pixelSize: 15
                    font.family: "Segoe UI"
                    wrapMode: Text.WordWrap
                }

                Text {
                    width: parent.width
                    text: "Every tab runs in its own process, so the other tabs and the " +
                          "browser window were not affected."
                    color: "#5f6a80"
                    font.pixelSize: 14
                    font.family: "Segoe UI"
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: 128
                    height: 36
                    radius: 9
                    color: revive.containsMouse ? "#2f4a8f" : "#25304a"
                    border.color: "#3c4b74"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "Reload tab"
                        color: "#e8eeff"
                        font.pixelSize: 14
                        font.family: "Segoe UI"
                    }

                    MouseArea {
                        id: revive
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.current.restart()
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------- source panel

    Rectangle {
        id: sourcePanel

        property bool shown: false

        anchors.top: toolbar.bottom
        anchors.bottom: statusbar.top
        anchors.right: parent.right
        width: shown && !window.barsHidden ? Math.min(560, window.width * 0.45) : 0
        clip: true
        color: "#0b0d13"

        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: "#1f2431"
        }

        Text {
            id: sourceHeading
            anchors.top: parent.top
            anchors.topMargin: 14
            anchors.left: parent.left
            anchors.leftMargin: 18
            text: "Document source"
            color: "#7f8aa3"
            font.pixelSize: 12
            font.family: "Segoe UI"
        }

        Flickable {
            anchors.top: sourceHeading.bottom
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            contentWidth: sourceText.implicitWidth
            contentHeight: sourceText.implicitHeight
            clip: true

            TextEdit {
                id: sourceText
                readOnly: true
                selectByMouse: true
                text: window.current ? window.current.sourceText : ""
                color: "#b9c4da"
                selectionColor: "#3a5da8"
                font.pixelSize: 13
                font.family: "Consolas"
            }
        }
    }

    // ----------------------------------------------------------- status line

    Rectangle {
        id: statusbar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: window.barsHidden ? 0 : 26
        visible: !window.barsHidden
        color: "#12151d"

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: "#1f2431"
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 320
            elide: Text.ElideRight
            font.pixelSize: 12
            font.family: "Segoe UI"
            color: "#6b7590"
            text: {
                if (window.hint.length > 0)
                    return window.hint;
                if (!window.current)
                    return "";
                if (window.current.crashed)
                    return "Renderer process ended";
                switch (window.current.status) {
                case BrowserTab.Loading: return "Loading " + window.current.url + "\u2026";
                case BrowserTab.Error:   return "Failed to load";
                case BrowserTab.Ready:   return window.current.title;
                default:                 return "Ready";
                }
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            font.pixelSize: 12
            font.family: "Consolas"
            color: window.current && window.current.httpStatus >= 400 ? "#ff8b98"
                 : (window.current && window.current.httpStatus > 0 ? "#6f9f7f" : "#6b7590")
            text: {
                if (!window.current)
                    return "";
                var parts = [];
                if (window.current.httpStatus > 0)
                    parts.push("HTTP " + window.current.httpStatus);
                if (window.current.byteCount > 0)
                    parts.push(window.current.byteCount + " bytes");
                if (window.current.rendererPid > 0)
                    parts.push("pid " + window.current.rendererPid);
                return parts.join("  \u00B7  ");
            }
        }
    }

    // ------------------------------------------------------------- shortcuts

    Shortcut { sequence: "Ctrl+T";        onActivated: tabs.handleShortcut("Ctrl+T") }
    Shortcut { sequence: "Ctrl+W";        onActivated: tabs.handleShortcut("Ctrl+W") }
    Shortcut { sequence: "Ctrl+Tab";      onActivated: tabs.handleShortcut("Ctrl+Tab") }
    Shortcut { sequence: "Ctrl+Shift+Tab";onActivated: tabs.handleShortcut("Ctrl+Shift+Tab") }
    Shortcut { sequence: "F5";            onActivated: tabs.handleShortcut("F5") }
    Shortcut { sequence: "Ctrl+R";        onActivated: tabs.handleShortcut("Ctrl+R") }
    Shortcut { sequence: "Alt+Left";      onActivated: tabs.handleShortcut("Alt+Left") }
    Shortcut { sequence: "Alt+Right";     onActivated: tabs.handleShortcut("Alt+Right") }
    Shortcut { sequence: "Ctrl+U";        onActivated: tabs.handleShortcut("Ctrl+U") }
    Shortcut { sequence: "Ctrl+L";        onActivated: tabs.handleShortcut("Ctrl+L") }
    Shortcut { sequence: "F11";           onActivated: tabs.handleShortcut("F11") }
    Shortcut { sequence: "Ctrl+Shift+F";  onActivated: tabs.handleShortcut("Ctrl+Shift+F") }

    Shortcut {
        sequence: "Escape"
        enabled: window.barsHidden
        onActivated: tabs.handleShortcut("Escape")
    }
}
