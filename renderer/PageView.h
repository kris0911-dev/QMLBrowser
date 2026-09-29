#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QQmlComponent;
QT_END_NAMESPACE

// The viewport of one tab, living inside its own renderer process.
//
// PageView downloads a QML document over HTTP, compiles it with QQmlComponent
// and parents the resulting item into itself. The document is given a private
// QQmlContext in which `browser` refers to this object, which is how a served
// page asks to be navigated somewhere else.
class PageView : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int httpStatus READ httpStatus NOTIFY httpStatusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY sourceTextChanged)
    Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY pageTitleChanged)

public:
    enum Status { Null, Loading, Ready, Error };
    Q_ENUM(Status)

    explicit PageView(QQuickItem *parent = nullptr);
    ~PageView() override;

    QUrl url() const { return m_url; }
    void setUrl(const QUrl &url);

    Status status() const { return m_status; }
    qreal progress() const { return m_progress; }
    int httpStatus() const { return m_httpStatus; }
    QString errorString() const { return m_errorString; }
    QString sourceText() const { return m_sourceText; }
    QString pageTitle() const { return m_pageTitle; }

    // Called from the served document, e.g. browser.navigate("about.qml").
    // Relative targets resolve against the current document URL.
    Q_INVOKABLE void navigate(const QString &target);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();

    // Kills this renderer process on purpose. Only this tab dies, which is the
    // whole point of giving every tab its own process.
    Q_INVOKABLE void crash();

    // Asks the browser window to hide or restore its chrome.
    Q_INVOKABLE void setFullScreen(bool on);

signals:
    void urlChanged();
    void statusChanged();
    void progressChanged();
    void httpStatusChanged();
    void errorStringChanged();
    void sourceTextChanged();
    void pageTitleChanged();

    // Forwarded to the browser process, which owns the history stack.
    void navigationRequested(const QUrl &url);
    void fullScreenRequested(bool on);
    void loadFinished(bool ok);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private slots:
    // Keeps pageTitle in sync when a document rebinds its `title` property.
    void refreshPageTitle();

private:
    void load();
    void onReplyFinished();
    void instantiate(const QByteArray &source, bool plainText);
    void onComponentReady();
    void fail(const QString &message);
    void clearContent();
    void setStatus(Status status);
    void setProgress(qreal progress);

    QUrl m_url;
    Status m_status = Null;
    qreal m_progress = 0.0;
    int m_httpStatus = 0;
    QString m_errorString;
    QString m_sourceText;
    QString m_pageTitle;
    // Set when the response is shown as text instead of compiled as QML.
    // documentText / documentTitle are injected into that document's context.
    bool m_plainText = false;
    QString m_documentText;
    QString m_documentTitle;

    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QQmlComponent *m_component = nullptr;
    QPointer<QQuickItem> m_content;
};
