#pragma once

#include <QDir>
#include <QHash>
#include <QTcpServer>

QT_BEGIN_NAMESPACE
class QTcpSocket;
QT_END_NAMESPACE

// A small static file server that hands out QML documents (and the images,
// JavaScript and qmldir files they reference) over plain HTTP/1.1.
//
// Only GET and HEAD are supported, which is all a QML browser needs. Requests
// are served straight from disk so that editing a .qml file under the document
// root is picked up by the next reload in the client.
class HttpServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit HttpServer(const QDir &documentRoot, QObject *parent = nullptr);

    QDir documentRoot() const { return m_root; }

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    struct Request
    {
        QByteArray method;
        QString target;   // raw request target, may contain a query string
        bool keepAlive = true;
    };

    void onReadyRead(QTcpSocket *socket);
    // Returns false when the connection must be closed (bad or partial data).
    bool dispatch(QTcpSocket *socket, const Request &request);

    void serveFile(QTcpSocket *socket, const Request &request, const QString &path,
                   const QFileInfo &file);
    void serveDirectory(QTcpSocket *socket, const Request &request, const QString &path,
                        const QDir &dir);
    void sendError(QTcpSocket *socket, const Request &request, int code, const QString &detail);

    void send(QTcpSocket *socket, const Request &request, int code, const QByteArray &contentType,
              const QByteArray &body);

    QDir m_root;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
