import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 800
    title: webView.title.length > 0
        ? webView.title + " \u2014 QMLBrowser"
        : "QMLBrowser"

    function normalizeUrl(input) {
        var text = input.trim()
        if (text.length === 0)
            return webView.url
        if (/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\//.test(text))
            return text
        if (text.indexOf(" ") === -1 && text.indexOf(".") !== -1)
            return "https://" + text
        return "https://duckduckgo.com/?q=" + encodeURIComponent(text)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 4

            ToolButton {
                text: "\u25C0"
                enabled: webView.canGoBack
                onClicked: webView.goBack()
                ToolTip.text: qsTr("Back")
                ToolTip.visible: hovered
            }
            ToolButton {
                text: "\u25B6"
                enabled: webView.canGoForward
                onClicked: webView.goForward()
                ToolTip.text: qsTr("Forward")
                ToolTip.visible: hovered
            }
            ToolButton {
                text: webView.loading ? "\u2715" : "\u21BB"
                onClicked: webView.loading ? webView.stop() : webView.reload()
                ToolTip.text: webView.loading ? qsTr("Stop") : qsTr("Reload")
                ToolTip.visible: hovered
            }

            TextField {
                id: urlBar
                Layout.fillWidth: true
                placeholderText: qsTr("Search or enter address")
                selectByMouse: true
                text: webView.url
                onAccepted: webView.url = window.normalizeUrl(text)
            }
        }
    }

    ProgressBar {
        id: progress
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 3
        z: 10
        from: 0
        to: 100
        value: webView.loadProgress
        visible: webView.loading
    }

    WebEngineView {
        id: webView
        anchors.fill: parent
        url: window.normalizeUrl(startupUrl)

        onUrlChanged: {
            if (!urlBar.activeFocus)
                urlBar.text = url
        }
    }
}
