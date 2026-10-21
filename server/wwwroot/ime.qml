import QtQuick
import "shared"

// IME test bench built only from core Qt Quick text items, so it loads even
// where QtQuick.Controls is unavailable. ime-controls.qml has the Controls set.
Page {
    id: page

    property string title: "QML Browser — IME test"

    // The text item that currently has active focus, for the state panel.
    property Item activeField: null
    property string activeLabel: "—"

    function log(source, message) {
        events.insert(0, { line: Qt.formatTime(new Date(), "hh:mm:ss.zzz") + "  " +
                                 source + "  " + message })
        while (events.count > 14)
            events.remove(events.count - 1)
    }

    ListModel { id: events }

    heading: "IME test bench"
    subheading: "Click a field and type with an input method (Pinyin, Kotoeri, " +
                "Telex…). The state panel shows the preedit string, composition flag and " +
                "cursor rectangle of whichever field has focus, and the log lists the key " +
                "and composition events the page actually received."

    // A labelled single-line field. Everything that matters to an IME is
    // forwarded from the outside: hints, echo mode, validator, read-only.
    component Field: Column {
        id: field

        property string label: ""
        property string placeholder: ""
        property alias text: input.text
        property int echoMode: TextInput.Normal
        property int inputMethodHints: Qt.ImhNone
        property var validator: null
        property bool readOnly: false
        property int maximumLength: 32767
        property int horizontalAlignment: TextInput.AlignLeft

        width: (parent.width - 20) / 2
        spacing: 6

        Text {
            text: field.label
            color: "#8d97ac"
            font.pixelSize: 13
            font.family: "Segoe UI"
        }

        Rectangle {
            width: parent.width
            height: 38
            radius: 8
            color: input.readOnly ? "#12151d" : "#0e1017"
            border.width: 1
            border.color: input.activeFocus ? "#8ab4ff" : "#2b3247"

            Behavior on border.color { ColorAnimation { duration: 120 } }

            TextInput {
                id: input
                x: 12
                width: parent.width - 24
                anchors.verticalCenter: parent.verticalCenter
                color: "#eef2fb"
                selectionColor: "#3f6bd8"
                selectedTextColor: "#ffffff"
                font.pixelSize: 16
                clip: true
                selectByMouse: true
                echoMode: field.echoMode
                inputMethodHints: field.inputMethodHints
                validator: field.validator
                readOnly: field.readOnly
                maximumLength: field.maximumLength
                horizontalAlignment: field.horizontalAlignment

                onActiveFocusChanged: {
                    if (activeFocus) {
                        page.activeField = input
                        page.activeLabel = field.label
                    }
                    page.log(field.label, activeFocus ? "focus in" : "focus out")
                }
                onPreeditTextChanged: page.log(field.label, "preedit \"" + preeditText + "\"")
                onInputMethodComposingChanged:
                    page.log(field.label, "composing " + inputMethodComposing)
                onTextEdited: page.log(field.label, "text \"" + text + "\"")
                Keys.onPressed: (event) => page.log(field.label, "key 0x" +
                    event.key.toString(16) + (event.text.length ? " \"" + event.text + "\"" : ""))
            }

            Text {
                anchors.fill: input
                verticalAlignment: Text.AlignVCenter
                text: field.placeholder
                color: "#4f5a72"
                font: input.font
                visible: input.text.length === 0 && input.preeditText.length === 0
            }
        }
    }

    Card {
        title: "Live input-method state"
        accent: "#7ce3b0"

        Column {
            width: parent.width
            spacing: 6

            StateRow { key: "focused item";   value: page.activeLabel }
            StateRow { key: "text";           value: page.activeField ? JSON.stringify(page.activeField.text) : "—" }
            StateRow { key: "preeditText";    value: page.activeField ? JSON.stringify(page.activeField.preeditText) : "—"
                       highlight: page.activeField !== null && page.activeField.preeditText.length > 0 }
            StateRow { key: "composing";      value: page.activeField ? String(page.activeField.inputMethodComposing) : "—"
                       highlight: page.activeField !== null && page.activeField.inputMethodComposing }
            StateRow { key: "cursorPosition"; value: page.activeField ? String(page.activeField.cursorPosition) : "—" }
            StateRow {
                key: "cursorRectangle"
                value: {
                    if (!page.activeField)
                        return "—"
                    const r = page.activeField.cursorRectangle
                    const p = page.activeField.mapToItem(null, r.x, r.y)
                    return "local " + Math.round(r.x) + "," + Math.round(r.y) + "  " +
                           Math.round(r.width) + "×" + Math.round(r.height) +
                           "    scene " + Math.round(p.x) + "," + Math.round(p.y)
                }
            }
            StateRow { key: "inputMethodHints"; value: page.activeField ? "0x" + page.activeField.inputMethodHints.toString(16) : "—" }
            StateRow { key: "Qt.inputMethod.locale";  value: Qt.inputMethod.locale.name }
            StateRow { key: "Qt.inputMethod.visible"; value: String(Qt.inputMethod.visible) }
        }
    }

    component StateRow: Row {
        property string key: ""
        property string value: ""
        property bool highlight: false

        spacing: 14

        Text {
            width: 190
            text: parent.key
            color: "#7d8ba6"
            font.pixelSize: 14
            font.family: "Consolas"
        }
        Text {
            text: parent.value
            color: parent.highlight ? "#f0b775" : "#d7deee"
            font.pixelSize: 14
            font.family: "Consolas"
        }
    }

    Card {
        title: "Single-line fields"

        Flow {
            width: parent.width
            spacing: 20

            Field { label: "Plain"; placeholder: "Type anything — 你好, こんにちは" }
            Field { label: "Prefilled"; text: "Edit 中文 in the middle" }
            Field {
                label: "Password (IME should be off)"
                placeholder: "secret"
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhHiddenText | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            }
            Field {
                label: "Password, echo on edit"
                placeholder: "secret"
                echoMode: TextInput.PasswordEchoOnEdit
            }
            Field {
                label: "Digits only (hint + validator)"
                placeholder: "0–99999"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: 99999 }
            }
            Field {
                label: "Email (Latin hint)"
                placeholder: "name@example.com"
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhLatinOnly
            }
            Field {
                label: "No predictive text"
                placeholder: "Prediction disabled"
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            }
            Field {
                label: "Max length 8"
                placeholder: "Composition at the limit"
                maximumLength: 8
            }
            Field {
                label: "Right-aligned"
                placeholder: "Candidate window should follow"
                horizontalAlignment: TextInput.AlignRight
            }
            Field {
                label: "Read-only"
                text: "IME must not edit this"
                readOnly: true
            }
        }
    }

    Card {
        title: "Multi-line text"
        accent: "#f0b775"

        Text {
            width: parent.width
            text: "A TextEdit with word wrap. Compose across a line break and near the bottom edge " +
                  "to check that the candidate window tracks the cursor."
            color: "#98a2b8"
            font.pixelSize: 15
            font.family: "Segoe UI"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            width: parent.width
            height: Math.max(140, area.contentHeight + 24)
            radius: 8
            color: "#0e1017"
            border.width: 1
            border.color: area.activeFocus ? "#f0b775" : "#2b3247"

            TextEdit {
                id: area
                x: 12
                y: 12
                width: parent.width - 24
                height: parent.height - 24
                color: "#eef2fb"
                selectionColor: "#3f6bd8"
                selectedTextColor: "#ffffff"
                font.pixelSize: 16
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                text: "第一行 / 一行目\nSecond line in English."

                onActiveFocusChanged: {
                    if (activeFocus) {
                        page.activeField = area
                        page.activeLabel = "TextEdit"
                    }
                    page.log("TextEdit", activeFocus ? "focus in" : "focus out")
                }
                onPreeditTextChanged: page.log("TextEdit", "preedit \"" + preeditText + "\"")
                onInputMethodComposingChanged:
                    page.log("TextEdit", "composing " + inputMethodComposing)
                Keys.onPressed: (event) => page.log("TextEdit", "key 0x" +
                    event.key.toString(16) + (event.text.length ? " \"" + event.text + "\"" : ""))
            }
        }
    }

    Card {
        title: "Event log"
        accent: "#ff8095"

        Text {
            text: "Clear log"
            color: clear.containsMouse ? "#ffa8b6" : "#ff8095"
            font.pixelSize: 14
            font.family: "Segoe UI"
            font.underline: clear.containsMouse

            MouseArea {
                id: clear
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: events.clear()
            }
        }

        Rectangle {
            width: parent.width
            height: 26 + 14 * 20
            radius: 8
            color: "#0b0d13"

            Column {
                x: 12
                y: 12
                width: parent.width - 24

                Repeater {
                    model: events

                    Text {
                        required property string line

                        width: parent.width
                        text: line
                        color: "#b8c2d6"
                        font.pixelSize: 13
                        font.family: "Consolas"
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: "No events yet — focus a field and start typing."
                color: "#4f5a72"
                font.pixelSize: 14
                font.family: "Segoe UI"
                visible: events.count === 0
            }
        }
    }

    Row {
        spacing: 22
        NavLink { text: "← Home"; url: "index.qml" }
        NavLink { text: "Qt Quick Controls version →"; url: "ime-controls.qml"; color: "#7ce3b0" }
    }
}
