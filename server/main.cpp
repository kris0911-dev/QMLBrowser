#include "HttpServer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QTextStream>

namespace {

// Finds the wwwroot folder. Looking up the source tree means the server run
// straight out of bin\x64\Debug still serves the files you edit in server\wwwroot.
QString findDocumentRoot()
{
    const QByteArray fromEnv = qgetenv("QMLSERVER_ROOT");
    if (!fromEnv.isEmpty())
        return QString::fromLocal8Bit(fromEnv);

    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.exists(QStringLiteral("wwwroot")))
        return dir.absoluteFilePath(QStringLiteral("wwwroot"));

    for (int i = 0; i < 6; ++i) {
        if (dir.exists(QStringLiteral("server/wwwroot")))
            return dir.absoluteFilePath(QStringLiteral("server/wwwroot"));
        if (!dir.cdUp())
            break;
    }

    return QDir::current().absoluteFilePath(QStringLiteral("wwwroot"));
}

// Printed to the console on purpose. Qt's own showHelp() opens a message box
// when the process has no console window, which hides the text from a terminal.
int printHelp(QTextStream &stream, const QCommandLineParser &parser, const QString &problem, int code)
{
    if (!problem.isEmpty())
        stream << problem << "\n\n";
    stream << parser.helpText() << Qt::flush;
    return code;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QmlServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
            "Serves .qml documents over HTTP for QmlBrowser.\n"
            "\n"
            "One dash or two both work: -port 9000 and --port 9000 are the same.\n"
            "With no arguments the server listens on http://127.0.0.1:8080/ and\n"
            "serves server\\wwwroot from the source tree.\n"
            "\n"
            "Examples:\n"
            "  QmlServer.exe\n"
            "  QmlServer.exe -port 9000\n"
            "  QmlServer.exe -root D:\\my\\qml\\site -address 0.0.0.0\n"
            "  QmlServer.exe -public"));
    // Windows-style -port and -public, not a bundle of short flags.
    // Otherwise -public is read as -p ublic and reported as a bad port.
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption portOption(
            { QStringLiteral("p"), QStringLiteral("port") },
            QStringLiteral("Port to listen on, from 1 to 65535. Default: 8080."),
            QStringLiteral("port"), QStringLiteral("8080"));
    const QCommandLineOption rootOption(
            { QStringLiteral("r"), QStringLiteral("root") },
            QStringLiteral("Folder of .qml files. Default: server\\wwwroot, or QMLSERVER_ROOT."),
            QStringLiteral("directory"));
    const QCommandLineOption addressOption(
            { QStringLiteral("a"), QStringLiteral("address") },
            QStringLiteral("Listen address. Default: 127.0.0.1. Use 0.0.0.0 for every interface."),
            QStringLiteral("ip"));
    const QCommandLineOption publicOption(
            QStringLiteral("public"),
            QStringLiteral("Listen on every interface. Same as -address 0.0.0.0."));
    parser.addOption(portOption);
    parser.addOption(rootOption);
    parser.addOption(addressOption);
    parser.addOption(publicOption);

    QTextStream out(stdout);
    QTextStream err(stderr);

    // A bad argument prints the full help. -help does the same, without an error.
    if (!parser.parse(QCoreApplication::arguments()))
        return printHelp(err, parser, parser.errorText(), 1);
    if (parser.isSet(QStringLiteral("help")) || parser.isSet(QStringLiteral("help-all")))
        return printHelp(out, parser, QString(), 0);
    if (parser.isSet(QStringLiteral("version"))) {
        out << QCoreApplication::applicationName() << ' '
            << QCoreApplication::applicationVersion() << '\n'
            << Qt::flush;
        return 0;
    }

    const QString portText = parser.value(portOption);
    bool portOk = false;
    const uint portValue = portText.toUInt(&portOk);
    if (!portOk || portValue == 0 || portValue > 65535) {
        return printHelp(err, parser,
                         QStringLiteral("Port must be a number from 1 to 65535, not \"%1\".")
                                 .arg(portText),
                         2);
    }
    const quint16 port = static_cast<quint16>(portValue);

    if (parser.isSet(publicOption) && parser.isSet(addressOption)) {
        return printHelp(err, parser,
                         QStringLiteral("Use either -address or -public, not both."), 2);
    }

    QString host = QStringLiteral("127.0.0.1");
    if (parser.isSet(publicOption))
        host = QStringLiteral("0.0.0.0");
    else if (parser.isSet(addressOption))
        host = parser.value(addressOption).trimmed();

    QHostAddress address;
    if (host == QLatin1String("0.0.0.0") || host == QLatin1String("*")) {
        host = QStringLiteral("0.0.0.0");
        address = QHostAddress::Any;
    } else if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0
               || host == QLatin1String("127.0.0.1")) {
        host = QStringLiteral("127.0.0.1");
        address = QHostAddress::LocalHost;
    } else if (!address.setAddress(host)) {
        return printHelp(err, parser,
                         QStringLiteral("Address must be an IP address such as 127.0.0.1 or "
                                        "0.0.0.0, not \"%1\".")
                                 .arg(host),
                         2);
    }

    const QString rootPath = parser.isSet(rootOption) ? parser.value(rootOption) : findDocumentRoot();
    const QDir root(QDir(rootPath).absolutePath());
    if (!root.exists()) {
        return printHelp(err, parser,
                         QStringLiteral("No .qml folder at %1.\n"
                                        "Pass -root with the directory that contains the pages.")
                                 .arg(QDir::toNativeSeparators(root.absolutePath())),
                         2);
    }

    HttpServer server(root);
    if (!server.listen(address, port)) {
        err << "Cannot listen on " << host << ':' << port << ": " << server.errorString() << '\n';
        return 1;
    }

    out << "QmlServer\n"
        << "  documents   " << QDir::toNativeSeparators(root.absolutePath()) << '\n'
        << "  address     http://" << host << ':' << port << "/\n"
        << "  home page   http://" << host << ':' << port << "/index.qml\n"
        << '\n'
        << "  -help       show the options\n"
        << "  Ctrl+C      stop the server\n"
        << '\n'
        << Qt::flush;

    return app.exec();
}
