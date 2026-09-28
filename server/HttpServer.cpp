#include "HttpServer.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QTextStream>
#include <QUrl>

namespace {

const char *reasonPhrase(int code)
{
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 414: return "URI Too Long";
    case 500: return "Internal Server Error";
    default:  return "Unknown";
    }
}

QByteArray contentTypeFor(const QString &suffix)
{
    static const QHash<QString, QByteArray> types = {
        { QStringLiteral("qml"),  "text/x-qml; charset=utf-8" },
        { QStringLiteral("js"),   "text/javascript; charset=utf-8" },
        { QStringLiteral("mjs"),  "text/javascript; charset=utf-8" },
        { QStringLiteral("json"), "application/json; charset=utf-8" },
        { QStringLiteral("txt"),  "text/plain; charset=utf-8" },
        { QStringLiteral("html"), "text/html; charset=utf-8" },
        { QStringLiteral("css"),  "text/css; charset=utf-8" },
        { QStringLiteral("svg"),  "image/svg+xml" },
        { QStringLiteral("png"),  "image/png" },
        { QStringLiteral("jpg"),  "image/jpeg" },
        { QStringLiteral("jpeg"), "image/jpeg" },
        { QStringLiteral("gif"),  "image/gif" },
        { QStringLiteral("webp"), "image/webp" },
        { QStringLiteral("ico"),  "image/vnd.microsoft.icon" },
        { QStringLiteral("ttf"),  "font/ttf" },
        { QStringLiteral("otf"),  "font/otf" },
    };
    // A bare "qmldir" has no suffix; the engine is happy with plain text.
    return types.value(suffix.toLower(), QByteArrayLiteral("text/plain; charset=utf-8"));
}

QString qmlEscape(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return out;
}

// Builds the QML document used for error pages and directory listings so that
// even a 404 is something the browser can actually render.
QByteArray qmlPage(const QString &title, const QString &heading, const QString &accent,
                   const QString &body)
{
    QString page;
    QTextStream out(&page);
    out << "import QtQuick\n\n"
        << "Rectangle {\n"
        << "    property string title: \"" << qmlEscape(title) << "\"\n"
        << "    color: \"#11131a\"\n"
        << "    Column {\n"
        << "        anchors.centerIn: parent\n"
        << "        width: Math.min(parent.width - 96, 720)\n"
        << "        spacing: 18\n"
        << "        Text {\n"
        << "            text: \"" << qmlEscape(heading) << "\"\n"
        << "            color: \"" << accent << "\"\n"
        << "            font.pixelSize: 44\n"
        << "            font.bold: true\n"
        << "        }\n"
        << "        Text {\n"
        << "            width: parent.width\n"
        << "            text: \"" << qmlEscape(body) << "\"\n"
        << "            color: \"#aeb6c6\"\n"
        << "            font.pixelSize: 16\n"
        << "            wrapMode: Text.WordWrap\n"
        << "        }\n"
        << "        Text {\n"
        << "            text: \"Open the start page\"\n"
        << "            color: home.containsMouse ? \"#b9d0ff\" : \"#8ab4ff\"\n"
        << "            font.pixelSize: 16\n"
        << "            font.underline: home.containsMouse\n"
        << "            MouseArea {\n"
        << "                id: home\n"
        << "                anchors.fill: parent\n"
        << "                hoverEnabled: true\n"
        << "                cursorShape: Qt.PointingHandCursor\n"
        << "                onClicked: browser.navigate(\"/\")\n"
        << "            }\n"
        << "        }\n"
        << "    }\n"
        << "}\n";
    return page.toUtf8();
}

} // namespace

HttpServer::HttpServer(const QDir &documentRoot, QObject *parent)
    : QTcpServer(parent)
    , m_root(documentRoot)
{
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        delete socket;
        return;
    }

    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_buffers.insert(socket, QByteArray());

    connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        m_buffers.remove(socket);
        socket->deleteLater();
    });
}

void HttpServer::onReadyRead(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());

    // Handle every complete request sitting in the buffer (keep-alive clients
    // such as QNetworkAccessManager happily pipeline image requests).
    forever {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (buffer.size() > 16 * 1024) {
                sendError(socket, Request{ "GET", QString(), false }, 414, QString());
                socket->disconnectFromHost();
            }
            return;
        }

        const QByteArray head = buffer.left(headerEnd);
        buffer.remove(0, headerEnd + 4);

        const QList<QByteArray> lines = head.split('\n');
        const QList<QByteArray> parts = lines.value(0).trimmed().split(' ');
        if (parts.size() < 3) {
            sendError(socket, Request{ "GET", QString(), false }, 400, QString());
            socket->disconnectFromHost();
            return;
        }

        Request request;
        request.method = parts.at(0).toUpper();
        request.target = QString::fromUtf8(parts.at(1));
        // HTTP/1.1 stays open unless the client asks to close. HTTP/1.0 closes
        // unless Connection says otherwise. The loop below treats any value
        // other than "close" as keep-alive, including "keep-alive" on 1.0.
        request.keepAlive = !parts.at(2).trimmed().endsWith("1.0");

        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            if (line.startsWith("Connection:")) {
                const QByteArray value = line.mid(11).trimmed().toLower();
                request.keepAlive = (value != "close");
            }
        }

        if (!dispatch(socket, request) || !request.keepAlive) {
            socket->disconnectFromHost();
            return;
        }
    }
}

bool HttpServer::dispatch(QTcpSocket *socket, const Request &request)
{
    if (request.method != "GET" && request.method != "HEAD") {
        sendError(socket, request, 405, QStringLiteral("Only GET and HEAD are supported."));
        return false;
    }

    // Strip the query and fragment, then percent-decode.
    QString path = request.target;
    const int cut = path.indexOf(QLatin1Char('?'));
    if (cut >= 0)
        path.truncate(cut);
    path = QUrl::fromPercentEncoding(path.toUtf8());

    if (!path.startsWith(QLatin1Char('/'))) {
        sendError(socket, request, 400, QStringLiteral("Malformed request target."));
        return false;
    }

    // Refuse anything that tries to climb out of the document root.
    for (const QString &segment : path.split(QLatin1Char('/'))) {
        if (segment == QLatin1String("..")) {
            // true keeps the connection. 405 returns false and drops it, because
            // a client that cannot speak GET will not recover; a bad path can.
            sendError(socket, request, 403, QStringLiteral("Path traversal is not allowed."));
            return true;
        }
    }

    const QString relative = path.mid(1);
    const QString absolute = QDir::cleanPath(m_root.absoluteFilePath(relative));
    if (!absolute.startsWith(m_root.absolutePath())) {
        sendError(socket, request, 403, QStringLiteral("Outside of the document root."));
        return true;
    }

    const QFileInfo info(absolute);
    if (info.isDir()) {
        serveDirectory(socket, request, path, QDir(absolute));
        return true;
    }

    if (!info.exists()) {
        sendError(socket, request, 404,
                  QStringLiteral("The server has no document at %1").arg(path));
        return true;
    }

    serveFile(socket, request, path, info);
    return true;
}

void HttpServer::serveFile(QTcpSocket *socket, const Request &request, const QString &path,
                           const QFileInfo &info)
{
    Q_UNUSED(path)

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        sendError(socket, request, 500, QStringLiteral("Cannot read %1").arg(info.fileName()));
        return;
    }

    send(socket, request, 200, contentTypeFor(info.suffix()), file.readAll());
}

void HttpServer::serveDirectory(QTcpSocket *socket, const Request &request, const QString &path,
                                const QDir &dir)
{
    // A directory serves its index.qml when there is one, exactly like a web
    // server serving index.html.
    if (dir.exists(QStringLiteral("index.qml"))) {
        serveFile(socket, request, path, QFileInfo(dir.absoluteFilePath("index.qml")));
        return;
    }

    const QString base = path.endsWith(QLatin1Char('/')) ? path : path + QLatin1Char('/');
    const QFileInfoList entries =
            dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

    QString listing;
    QTextStream out(&listing);
    out << "import QtQuick\n\n"
        << "Rectangle {\n"
        << "    property string title: \"Index of " << qmlEscape(base) << "\"\n"
        << "    color: \"#11131a\"\n"
        << "    Flickable {\n"
        << "        anchors.fill: parent\n"
        << "        anchors.margins: 40\n"
        << "        contentHeight: column.height\n"
        << "        clip: true\n"
        << "        Column {\n"
        << "            id: column\n"
        << "            width: parent.width\n"
        << "            spacing: 10\n"
        << "            Text {\n"
        << "                text: \"Index of " << qmlEscape(base) << "\"\n"
        << "                color: \"#f2f5ff\"\n"
        << "                font.pixelSize: 28\n"
        << "                font.bold: true\n"
        << "                bottomPadding: 12\n"
        << "            }\n";

    if (base != QLatin1String("/")) {
        out << "            Text {\n"
            << "                text: \"../\"\n"
            << "                color: up.containsMouse ? \"#8ab4ff\" : \"#7d8ba6\"\n"
            << "                font.pixelSize: 16\n"
            << "                font.family: \"Segoe UI\"\n"
            << "                MouseArea {\n"
            << "                    id: up\n"
            << "                    anchors.fill: parent\n"
            << "                    hoverEnabled: true\n"
            << "                    cursorShape: Qt.PointingHandCursor\n"
            << "                    onClicked: browser.navigate(\"..\")\n"
            << "                }\n"
            << "            }\n";
    }

    int n = 0;
    for (const QFileInfo &entry : entries) {
        const QString name = entry.isDir() ? entry.fileName() + QLatin1Char('/') : entry.fileName();
        const QString size = entry.isDir() ? QStringLiteral("dir")
                                           : QStringLiteral("%1 B").arg(entry.size());
        // Every row needs its own id, otherwise the document will not compile.
        const QString link = QStringLiteral("link%1").arg(n++);
        out << "            Row {\n"
            << "                spacing: 14\n"
            << "                Text {\n"
            << "                    text: \"" << qmlEscape(name) << "\"\n"
            << "                    color: " << link << ".containsMouse ? \"#8ab4ff\" : \"#d7deee\"\n"
            << "                    font.pixelSize: 16\n"
            << "                    font.family: \"Segoe UI\"\n"
            << "                    font.underline: " << link << ".containsMouse\n"
            << "                    MouseArea {\n"
            << "                        id: " << link << "\n"
            << "                        anchors.fill: parent\n"
            << "                        hoverEnabled: true\n"
            << "                        cursorShape: Qt.PointingHandCursor\n"
            << "                        onClicked: browser.navigate(\"" << qmlEscape(name) << "\")\n"
            << "                    }\n"
            << "                }\n"
            << "                Text {\n"
            << "                    text: \"" << size << "\"\n"
            << "                    color: \"#6b7487\"\n"
            << "                    font.pixelSize: 14\n"
            << "                    font.family: \"Segoe UI\"\n"
            << "                }\n"
            << "            }\n";
    }

    out << "        }\n    }\n}\n";
    send(socket, request, 200, QByteArrayLiteral("text/x-qml; charset=utf-8"), listing.toUtf8());
}

void HttpServer::sendError(QTcpSocket *socket, const Request &request, int code,
                           const QString &detail)
{
    const QString heading = QStringLiteral("%1 %2").arg(code).arg(QLatin1String(reasonPhrase(code)));
    const QByteArray body = qmlPage(heading, heading, QStringLiteral("#ff6b81"), detail);
    send(socket, request, code, QByteArrayLiteral("text/x-qml; charset=utf-8"), body);
}

void HttpServer::send(QTcpSocket *socket, const Request &request, int code,
                      const QByteArray &contentType, const QByteArray &body)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(code) + ' ' + reasonPhrase(code) + "\r\n";
    response += "Server: QmlServer/1.0 (Qt " QT_VERSION_STR ")\r\n";
    response += "Date: " + QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date).toUtf8() + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    // QML is edited live during development, so never let the client cache it.
    response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    response += QByteArray("Connection: ") + (request.keepAlive ? "keep-alive" : "close") + "\r\n";
    response += "\r\n";

    socket->write(response);
    if (request.method != "HEAD")
        socket->write(body);
    socket->flush();

    QTextStream(stdout) << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")) << "  "
                        << QString::fromUtf8(request.method).leftJustified(5) << ' '
                        << QString::number(code) << "  "
                        << QString::number(body.size()).rightJustified(7) << " B  "
                        << (request.target.isEmpty() ? QStringLiteral("-") : request.target) << '\n'
                        << Qt::flush;
}
