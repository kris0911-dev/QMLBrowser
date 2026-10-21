// Never loaded. windeployqt scans this folder for QML imports, so that the
// modules pages on the sample site use are deployed next to QmlRenderer.exe.
// The renderer's own QML (ui\) imports nothing beyond QtQuick, and a page is
// downloaded at run time, so nothing else tells windeployqt about them.
import QtQuick
import QtQuick.Controls.Basic

Item {}
