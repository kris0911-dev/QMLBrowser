#pragma once

#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

// Which program serves an app://<name> address.
//
// "widgets" is built in and runs WidgetApp. More applications are listed in
// apps.json next to the browser executable (on macOS, next to the
// QmlBrowser.app bundle, or inside it beside the executable):
//
//     {
//         "notes":  "Notes",
//         "viewer": "../tools/Viewer.exe",
//         "editor": { "program": "C:/Apps/Editor/Editor.exe", "arguments": ["--compact"] }
//     }
//
// A bare name is looked up next to the browser, the way QmlRenderer and
// WidgetApp are. Anything with a slash is a path, relative to apps.json. On
// macOS a path to an .app bundle means the executable inside it. An entry in
// apps.json replaces a built-in one of the same name. The file is read once,
// when the first tab needs it.
//
// Every listed program must link QmlBrowserHost (apphost/) and call its
// prepare() and attach(); the browser starts it with --channel and --tab
// after the listed arguments.
namespace AppRegistry {

struct App {
    QString program;
    QStringList arguments;
};

// The registered name for an app:// address (its host, lowercased), or an
// empty string when the address is not an app:// address or names no app.
QString appFor(const QUrl &url);

// The program and arguments for a name appFor() returned.
App find(const QString &name);

// A helper program shipped with the browser, such as QmlRenderer: next to
// the browser executable, or on macOS next to the browser's app bundle.
QString bundledProgram(const QString &name);

} // namespace AppRegistry
