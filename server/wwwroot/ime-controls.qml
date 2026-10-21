import QtQuick
// The Basic style is pinned so the page looks the same on every platform and
// does not depend on a native style plugin inside the offscreen renderer.
import QtQuick.Controls.Basic
import "shared"

Page {
    id: page

    property string title: "QML Browser — IME test (Controls)"

    property Item activeField: null
    property string activeLabel: "—"
    property string lastEvent: "—"

    function track(label, item) {
        if (item.activeFocus) {
            activeField = item
            activeLabel = label
        }
    }

    heading: "IME test — Qt Quick Controls"
    subheading: "The same checks as ime.qml, using TextField, TextArea, editable ComboBox and " +
                "SpinBox, plus non-text controls to confirm that focus moves away from text " +
                "input cleanly and the IME closes."

    component Label2: Text {
        color: "#8d97ac"
        font.pixelSize: 13
        font.family: "Segoe UI"
    }

    Card {
        title: "Live input-method state"
        accent: "#7ce3b0"

        Repeater {
            model: [
                { k: "focused control", v: "label" },
                { k: "text",            v: "text" },
                { k: "preeditText",     v: "preedit" },
                { k: "composing",       v: "composing" },
                { k: "cursorRectangle", v: "rect" },
                { k: "last event",      v: "event" }
            ]

            Row {
                id: stateRow
                required property var modelData

                spacing: 14

                Text {
                    width: 190
                    text: stateRow.modelData.k
                    color: "#7d8ba6"
                    font.pixelSize: 14
                    font.family: "Consolas"
                }
                Text {
                    color: "#d7deee"
                    font.pixelSize: 14
                    font.family: "Consolas"
                    text: {
                        const f = page.activeField
                        switch (stateRow.modelData.v) {
                        case "label":     return page.activeLabel
                        case "event":     return page.lastEvent
                        }
                        if (!f)
                            return "—"
                        switch (stateRow.modelData.v) {
                        case "text":      return JSON.stringify(f.text)
                        case "preedit":   return JSON.stringify(f.preeditText)
                        case "composing": return String(f.inputMethodComposing)
                        case "rect": {
                            const r = f.cursorRectangle
                            const p = f.mapToItem(null, r.x, r.y)
                            return "local " + Math.round(r.x) + "," + Math.round(r.y) + "    scene " +
                                   Math.round(p.x) + "," + Math.round(p.y)
                        }
                        }
                        return ""
                    }
                }
            }
        }
    }

    Card {
        title: "Text controls"

        Grid {
            id: grid
            width: parent.width
            columns: 2
            columnSpacing: 20
            rowSpacing: 14

            readonly property real cell: (width - columnSpacing) / 2

            Column {
                spacing: 6
                Label2 { text: "TextField" }
                TextField {
                    width: grid.cell
                    placeholderText: "你好 / こんにちは"
                    onActiveFocusChanged: page.track("TextField", this)
                    onPreeditTextChanged: page.lastEvent = "preedit \"" + preeditText + "\""
                    onTextEdited: page.lastEvent = "textEdited"
                    onAccepted: page.lastEvent = "accepted \"" + text + "\""
                }
            }

            Column {
                spacing: 6
                Label2 { text: "TextField — password" }
                TextField {
                    width: grid.cell
                    placeholderText: "IME should be disabled"
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    onActiveFocusChanged: page.track("TextField (password)", this)
                }
            }

            Column {
                spacing: 6
                Label2 { text: "TextField — digits only" }
                TextField {
                    width: grid.cell
                    placeholderText: "0–99999"
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 0; top: 99999 }
                    onActiveFocusChanged: page.track("TextField (digits)", this)
                }
            }

            Column {
                spacing: 6
                Label2 { text: "TextField — read-only" }
                TextField {
                    width: grid.cell
                    text: "Read-only 只读"
                    readOnly: true
                    onActiveFocusChanged: page.track("TextField (read-only)", this)
                }
            }

            Column {
                spacing: 6
                Label2 { text: "ComboBox — editable" }
                ComboBox {
                    id: combo
                    width: grid.cell
                    editable: true
                    model: ["東京", "北京", "Hà Nội", "New York"]
                    onAccepted: page.lastEvent = "combo accepted \"" + editText + "\""
                    // The editor is the contentItem, a TextInput.
                    Connections {
                        target: combo.contentItem
                        function onActiveFocusChanged() {
                            page.track("ComboBox editor", combo.contentItem)
                        }
                    }
                }
            }

            Column {
                spacing: 6
                Label2 { text: "SpinBox — editable" }
                SpinBox {
                    id: spin
                    width: grid.cell
                    editable: true
                    from: 0
                    to: 1000
                    value: 42
                    Connections {
                        target: spin.contentItem
                        function onActiveFocusChanged() {
                            page.track("SpinBox editor", spin.contentItem)
                        }
                    }
                }
            }
        }

        Label2 { text: "TextArea in a ScrollView" }

        ScrollView {
            width: parent.width
            height: 160

            TextArea {
                wrapMode: TextArea.Wrap
                placeholderText: "Multi-line composition. Type past the bottom so it scrolls."
                text: "第一行 / 一行目\nSecond line in English.\n"
                onActiveFocusChanged: page.track("TextArea", this)
                onPreeditTextChanged: page.lastEvent = "preedit \"" + preeditText + "\""
            }
        }
    }

    Card {
        title: "Other input controls"
        accent: "#f0b775"

        Text {
            width: parent.width
            text: "None of these accept text. Clicking them while composing should commit or " +
                  "cancel the preedit and hide the candidate window."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Flow {
            width: parent.width
            spacing: 16

            Button {
                text: "Button"
                onClicked: page.lastEvent = "button clicked"
            }
            CheckBox {
                text: "CheckBox"
                onToggled: page.lastEvent = "checkbox " + checked
            }
            Switch {
                text: "Switch"
                onToggled: page.lastEvent = "switch " + checked
            }
            RadioButton { text: "Radio A"; checked: true }
            RadioButton { text: "Radio B" }
        }

        Row {
            spacing: 16

            Slider {
                id: slider
                width: 260
                from: 0
                to: 100
                value: 30
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(slider.value)
                color: "#f0b775"
                font.pixelSize: 15
                font.family: "Segoe UI"
            }
        }

        Row {
            spacing: 16

            ComboBox {
                width: 200
                model: ["Non-editable", "ComboBox", "Popup list"]
            }
            Dial {
                width: 64
                height: 64
            }
        }
    }

    Row {
        spacing: 22
        NavLink { text: "← IME test bench"; url: "ime.qml" }
        NavLink { text: "Home"; url: "index.qml"; color: "#8ab4ff" }
    }
}
