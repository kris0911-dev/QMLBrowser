#include "IpcChannel.h"

#include <QJsonDocument>
#include <QLocalSocket>

IpcChannel::IpcChannel(QLocalSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
{
    connect(m_socket, &QLocalSocket::readyRead, this, &IpcChannel::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &IpcChannel::disconnected);
}

bool IpcChannel::isOpen() const
{
    return m_socket && m_socket->state() == QLocalSocket::ConnectedState;
}

void IpcChannel::close()
{
    if (m_socket && m_socket->state() != QLocalSocket::UnconnectedState)
        m_socket->disconnectFromServer();
}

void IpcChannel::send(const QJsonObject &message)
{
    if (!isOpen())
        return;

    // Compact form escapes newlines inside strings, so one '\n' is a frame even
    // when the payload is a whole QML document. flush() matters: a state update
    // during resize must not sit in the kernel buffer until the next event.
    m_socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    m_socket->flush();
}

void IpcChannel::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    forever {
        const qsizetype end = m_buffer.indexOf('\n');
        if (end < 0)
            return;

        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        if (line.isEmpty())
            continue;

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        // A bad line is dropped rather than closing the tab. One corrupt frame
        // should not take down a renderer that is otherwise still drawing.
        if (error.error == QJsonParseError::NoError && document.isObject())
            emit received(document.object());
    }
}
