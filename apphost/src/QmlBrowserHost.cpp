#include "QmlBrowserHost.h"

#include "BrowserBridge.h"
#include "NativePlatform.h"
#include "OffscreenPlatform.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <cstring>

namespace {

struct Host {
    bool hosted = false;
    QmlBrowserHost::Graphics graphics = QmlBrowserHost::Graphics::Software;
    QString channel;
    int tab = 0;
    QPointer<BrowserBridge> bridge;
    QmlBrowserHost::NavigateHandler navigateHandler;
};

Host &host()
{
    static Host instance;
    return instance;
}

// "--name value" or "--name=value". Advances `i` past a separate value.
bool takeOption(const char *name, int argc, char **argv, int &i, QString &value)
{
    const char *arg = argv[i];
    const size_t length = std::strlen(name);
    if (std::strncmp(arg, name, length) != 0)
        return false;
    if (arg[length] == '=') {
        value = QString::fromLocal8Bit(arg + length + 1);
        return true;
    }
    if (arg[length] != '\0')
        return false;
    if (i + 1 < argc)
        value = QString::fromLocal8Bit(argv[++i]);
    return true;
}

void setUpApplication()
{
    if (host().graphics == QmlBrowserHost::Graphics::Gpu) {
        NativePlatform::hideFromDock();
        return;
    }
    OffscreenPlatform::resizeScreen(1280, 800);
    OffscreenPlatform::registerFontCollections();
    OffscreenPlatform::applyInterfaceFont();
}

QmlBrowserHost::Graphics graphicsFromEnvironment(QmlBrowserHost::Graphics fallback)
{
    const QByteArray value = qgetenv("QMLBROWSERHOST_GRAPHICS").trimmed().toLower();
    if (value == "gpu")
        return QmlBrowserHost::Graphics::Gpu;
    if (value == "software")
        return QmlBrowserHost::Graphics::Software;
    if (!value.isEmpty())
        qWarning("QmlBrowserHost: QMLBROWSERHOST_GRAPHICS is \"%s\"; expected software or gpu.",
                 value.constData());
    return fallback;
}

} // namespace

bool QmlBrowserHost::prepare(int &argc, char **argv, Graphics graphics)
{
    Host &state = host();
    state.graphics = graphicsFromEnvironment(graphics);

    // QApplication must not see these: an application's own command-line
    // parser would reject them as unknown options.
    int kept = 1;
    for (int i = 1; i < argc; ++i) {
        QString value;
        if (takeOption("--channel", argc, argv, i, value)) {
            state.hosted = true;
            state.channel = value;
        } else if (takeOption("--tab", argc, argv, i, value)) {
            state.tab = value.toInt();
        } else {
            argv[kept++] = argv[i];
        }
    }
    if (kept < argc)
        argv[kept] = nullptr;
    argc = kept;

    if (state.hosted) {
        if (state.graphics == Graphics::Gpu)
            NativePlatform::select();
        else
            OffscreenPlatform::select();
        // Runs inside the QApplication constructor, once the platform exists
        // and before the application creates a widget. In Software mode a
        // font set any later would first resolve to a generic family, and Qt
        // spends most of a second building font aliases to find a substitute
        // for it.
        qAddPreRoutine(&setUpApplication);
    }
    return state.hosted;
}

bool QmlBrowserHost::isHosted()
{
    return host().hosted;
}

QmlBrowserHost::Graphics QmlBrowserHost::graphics()
{
    return host().graphics;
}

bool QmlBrowserHost::attach(QWidget *mainWindow)
{
    Host &state = host();
    if (!state.hosted)
        return false;
    if (!qobject_cast<QApplication *>(QCoreApplication::instance()) || !mainWindow) {
        qWarning("QmlBrowserHost::attach needs a QApplication and a main window.");
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(1); });
        return true;
    }
    if (state.bridge) {
        qWarning("QmlBrowserHost::attach was called twice; only one window is hosted.");
        return true;
    }

    // A child of the window: the bridge keeps plain pointers to it, so it
    // must never outlive it, whichever of the two the application ends first.
    auto *bridge = new BrowserBridge(mainWindow, state.graphics == Graphics::Gpu, mainWindow);
    state.bridge = bridge;
    bridge->setNavigateHandler(state.navigateHandler);
    if (!bridge->connectToBrowser(state.channel, state.tab)) {
        // exit() does nothing before exec() runs, so it waits for the loop.
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(1); });
    }
    return true;
}

void QmlBrowserHost::setNavigateHandler(NavigateHandler handler)
{
    Host &state = host();
    state.navigateHandler = std::move(handler);
    if (state.bridge)
        state.bridge->setNavigateHandler(state.navigateHandler);
}

void QmlBrowserHost::setPath(const QString &path)
{
    if (BrowserBridge *bridge = host().bridge)
        bridge->setPath(path);
}
