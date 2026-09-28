#include "BrowserHistory.h"

#include <QHostAddress>

BrowserHistory::BrowserHistory(QObject *parent)
    : QObject(parent)
{
}

QUrl BrowserHistory::currentUrl() const
{
    return (m_index >= 0 && m_index < m_entries.size()) ? m_entries.at(m_index) : QUrl();
}

QStringList BrowserHistory::entries() const
{
    QStringList list;
    list.reserve(m_entries.size());
    for (const QUrl &url : m_entries)
        list << url.toString();
    return list;
}

namespace {

// What the address bar should treat as a site, not as a document name.
// "127.0.0.1" has no scheme, so QUrl would otherwise resolve it against the
// open page and request /127.0.0.1, which is a 404. The same applies to
// "127.0.0.1:8080" (a scheme cannot start with a digit) and "localhost".
struct AbsoluteHttp {
    QString url;
    bool hasPort = false;
};

AbsoluteHttp asAbsoluteHttp(const QString &text)
{
    AbsoluteHttp none;
    if (text.contains(QLatin1String("://")) || text.startsWith(QLatin1Char('/'))
        || text.startsWith(QLatin1Char('.'))) {
        return none;
    }

    int headEnd = text.size();
    const int slash = text.indexOf(QLatin1Char('/'));
    const int query = text.indexOf(QLatin1Char('?'));
    if (slash >= 0)
        headEnd = slash;
    if (query >= 0 && query < headEnd)
        headEnd = query;

    const QString head = text.left(headEnd);
    const QString tail = text.mid(headEnd);
    if (head.isEmpty())
        return none;

    QString host = head;
    QString port;
    bool ipv6 = false;

    if (head.startsWith(QLatin1Char('['))) {
        const int end = head.indexOf(QLatin1Char(']'));
        if (end < 1)
            return none;
        host = head.mid(1, end - 1);
        ipv6 = true;
        if (end + 1 < head.size()) {
            if (head.at(end + 1) != QLatin1Char(':'))
                return none;
            port = head.mid(end + 2);
        }
    } else {
        QHostAddress whole;
        if (whole.setAddress(head)) {
            host = head;
            ipv6 = whole.protocol() == QAbstractSocket::IPv6Protocol;
        } else {
            const int colon = head.lastIndexOf(QLatin1Char(':'));
            if (colon > 0) {
                host = head.left(colon);
                port = head.mid(colon + 1);
            }
        }
    }

    if (!port.isEmpty()) {
        bool ok = false;
        const uint value = port.toUInt(&ok);
        if (!ok || value == 0 || value > 65535)
            return none;
    }

    const bool localhost = host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0;
    QHostAddress address;
    const bool ip = address.setAddress(host);
    if (ip && address.protocol() == QAbstractSocket::IPv6Protocol)
        ipv6 = true;
    // A name with an explicit port ("example.com:8080") is a site. A bare
    // name ("about.qml", "docs") stays a document relative to the open page.
    if (!localhost && !ip && port.isEmpty())
        return none;
    if (!localhost && !ip && host.isEmpty())
        return none;

    QString authority = ipv6 ? QLatin1Char('[') + host + QLatin1Char(']') : host;
    if (!port.isEmpty())
        authority += QLatin1Char(':') + port;

    AbsoluteHttp result;
    result.url = QStringLiteral("http://") + authority + tail;
    result.hasPort = !port.isEmpty();
    return result;
}

} // namespace

QUrl BrowserHistory::resolve(const QString &text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return QUrl();

    const AbsoluteHttp absolute = asAbsoluteHttp(trimmed);
    QString input = absolute.url.isEmpty() ? trimmed : absolute.url;

    QUrl candidate(input);

    // "/about.qml" or "about.qml" stay relative to the page that is open.
    if (candidate.isRelative()) {
        const QUrl base = currentUrl();
        if (base.isValid() && !base.isRelative())
            return base.resolved(candidate);
        return QUrl(QStringLiteral("http://") + trimmed);
    }

    const QString scheme = candidate.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")
        && scheme != QLatin1String("file") && scheme != QLatin1String("qrc")) {
        return QUrl(QStringLiteral("http://") + trimmed);
    }

    // "127.0.0.1" typed while already on that host keeps the open port, so it
    // opens the site root instead of falling through to port 80.
    if (!absolute.url.isEmpty() && !absolute.hasPort) {
        const QUrl base = currentUrl();
        if (base.port() > 0 && base.host().compare(candidate.host(), Qt::CaseInsensitive) == 0)
            candidate.setPort(base.port());
    }
    if ((scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        && candidate.path().isEmpty()) {
        candidate.setPath(QStringLiteral("/"));
    }

    return candidate;
}

void BrowserHistory::navigateTo(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return;

    if (currentUrl() == url) {
        // Re-navigating to the same entry is a no-op for history; the chrome
        // turns this into a reload.
        return;
    }

    if (m_index >= 0 && m_index < m_entries.size() - 1)
        m_entries.erase(m_entries.begin() + m_index + 1, m_entries.end());

    m_entries.append(url);
    m_index = m_entries.size() - 1;

    emit currentUrlChanged();
    emit changed();
}

void BrowserHistory::navigateToText(const QString &text)
{
    navigateTo(resolve(text));
}

void BrowserHistory::back()
{
    if (!canGoBack())
        return;
    --m_index;
    emit currentUrlChanged();
    emit changed();
}

void BrowserHistory::forward()
{
    if (!canGoForward())
        return;
    ++m_index;
    emit currentUrlChanged();
    emit changed();
}
