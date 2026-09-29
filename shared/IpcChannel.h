#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

QT_BEGIN_NAMESPACE
class QLocalSocket;
QT_END_NAMESPACE

// Newline-delimited JSON over a local socket (a named pipe on Windows).
//
// Compact JSON never contains a literal newline — line breaks inside strings are
// escaped as \n — so a single '\n' is a safe frame terminator even when a message
// carries a whole QML document.
class IpcChannel : public QObject
{
    Q_OBJECT

public:
    explicit IpcChannel(QLocalSocket *socket, QObject *parent = nullptr);

    QLocalSocket *socket() const { return m_socket; }
    bool isOpen() const;

    void send(const QJsonObject &message);
    // Hanging up is how either side says "we are done here".
    void close();

signals:
    void received(const QJsonObject &message);
    void disconnected();

private:
    void onReadyRead();

    QLocalSocket *m_socket = nullptr;
    QByteArray m_buffer;
};
