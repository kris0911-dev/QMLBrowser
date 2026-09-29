#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QList>

// Back/forward stack. The chrome binds the page view's url to `currentUrl`, so
// every navigation goes through here and the buttons stay consistent.
class BrowserHistory : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QUrl currentUrl READ currentUrl NOTIFY currentUrlChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    Q_PROPERTY(QStringList entries READ entries NOTIFY changed)

public:
    explicit BrowserHistory(QObject *parent = nullptr);

    QUrl currentUrl() const;
    bool canGoBack() const { return m_index > 0; }
    bool canGoForward() const { return m_index >= 0 && m_index < m_entries.size() - 1; }
    QStringList entries() const;

    // Pushes a new entry, discarding anything ahead of the current position.
    Q_INVOKABLE void navigateTo(const QUrl &url);
    // Same, but accepts what the user typed: "localhost:8080/x.qml" or "/x.qml".
    Q_INVOKABLE void navigateToText(const QString &text);
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();

    // Turns user input into an absolute URL. A host such as "127.0.0.1" is that
    // site; "about.qml" stays relative to the current page.
    Q_INVOKABLE QUrl resolve(const QString &text) const;

signals:
    void currentUrlChanged();
    void changed();

private:
    QList<QUrl> m_entries;
    int m_index = -1;
};
