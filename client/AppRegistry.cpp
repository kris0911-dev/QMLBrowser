#include "AppRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {

// Folders searched for apps.json, nearest first.
QStringList configFolders()
{
    const QDir dir(QCoreApplication::applicationDirPath());
    QStringList folders{ dir.absolutePath() };
#ifdef Q_OS_MACOS
    // Contents/MacOS inside QmlBrowser.app: also look beside the bundle.
    QDir root(dir);
    if (root.cdUp() && root.cdUp() && root.cdUp())
        folders.append(root.absolutePath());
#endif
    return folders;
}

QString resolveProgram(const QString &value, const QDir &base)
{
    if (!value.contains(QLatin1Char('/')) && !value.contains(QLatin1Char('\\')))
        return AppRegistry::bundledProgram(value);

    QString path = QDir::cleanPath(base.absoluteFilePath(QDir::fromNativeSeparators(value)));
#ifdef Q_OS_MACOS
    if (path.endsWith(QLatin1String(".app"))) {
        const QString name = QFileInfo(path).completeBaseName();
        path += QStringLiteral("/Contents/MacOS/") + name;
    }
#endif
    return path;
}

QHash<QString, AppRegistry::App> load()
{
    QHash<QString, AppRegistry::App> apps;
    apps.insert(QStringLiteral("widgets"), { AppRegistry::bundledProgram(QStringLiteral("WidgetApp")), {} });

    for (const QString &folder : configFolders()) {
        QFile file(QDir(folder).filePath(QStringLiteral("apps.json")));
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (!document.isObject()) {
            qWarning("QmlBrowser: %s is not a JSON object: %s", qPrintable(file.fileName()),
                     qPrintable(error.errorString()));
            break;
        }

        const QDir base(folder);
        const QJsonObject entries = document.object();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            AppRegistry::App app;
            if (it.value().isString()) {
                app.program = resolveProgram(it.value().toString(), base);
            } else if (it.value().isObject()) {
                const QJsonObject entry = it.value().toObject();
                app.program = resolveProgram(entry.value(QStringLiteral("program")).toString(), base);
                for (const QJsonValue &argument : entry.value(QStringLiteral("arguments")).toArray())
                    app.arguments.append(argument.toString());
            }
            if (app.program.isEmpty()) {
                qWarning("QmlBrowser: %s: \"%s\" names no program.", qPrintable(file.fileName()),
                         qPrintable(it.key()));
                continue;
            }
            // Host names are case-insensitive, and QUrl lowercases them.
            apps.insert(it.key().toLower(), app);
        }
        // The nearest apps.json wins; the others are not merged in.
        break;
    }
    return apps;
}

const QHash<QString, AppRegistry::App> &registry()
{
    static const QHash<QString, AppRegistry::App> apps = load();
    return apps;
}

} // namespace

QString AppRegistry::appFor(const QUrl &url)
{
    if (url.scheme() != QLatin1String("app"))
        return {};
    const QString name = url.host().toLower();
    return registry().contains(name) ? name : QString();
}

AppRegistry::App AppRegistry::find(const QString &name)
{
    return registry().value(name);
}

QString AppRegistry::bundledProgram(const QString &name)
{
    const QDir dir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
    const QString file = name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
            ? name : name + QStringLiteral(".exe");
    return dir.absoluteFilePath(file);
#elif defined(Q_OS_MACOS)
    const QString beside = dir.absoluteFilePath(name);
    if (QFileInfo::exists(beside))
        return beside;

    QDir root(dir);
    if (root.cdUp() && root.cdUp() && root.cdUp()) {
        const QString bundled = root.absoluteFilePath(
                name + QStringLiteral(".app/Contents/MacOS/") + name);
        if (QFileInfo::exists(bundled))
            return bundled;
    }
    return beside;
#else
    return dir.absoluteFilePath(name);
#endif
}
