#include "PageView.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlProperty>

#include <cstdlib>

PageView::PageView(QQuickItem *parent)
    : QQuickItem(parent)
    , m_network(new QNetworkAccessManager(this))
{
    // The served document paints. This item only hosts it, and clip keeps a
    // page that ignores its assigned size from drawing over the chrome.
    setFlag(ItemHasContents, false);
    setClip(true);
}

PageView::~PageView()
{
    stop();
}

void PageView::setUrl(const QUrl &url)
{
    if (m_url == url)
        return;

    m_url = url;
    emit urlChanged();
    load();
}

void PageView::navigate(const QString &target)
{
    // History lives in the browser process. Emitting the resolved URL is the
    // whole of this side; the browser pushes the stack and sends `navigate` back.
    if (target.isEmpty())
        return;

    const QUrl relative(target);
    const QUrl resolved = m_url.isValid() && !m_url.isRelative() ? m_url.resolved(relative)
                                                                 : relative;
    emit navigationRequested(resolved);
}

void PageView::reload()
{
    if (m_url.isValid())
        load();
}

void PageView::crash()
{
    // Deliberate abnormal termination so the browser process can show its
    // "tab stopped responding" page. Other tabs are unaffected.
    qWarning().noquote() << "Renderer asked to crash by" << m_url.toString();
    ::abort();
}

void PageView::setFullScreen(bool on)
{
    // The chrome belongs to the browser process, so this is only a request.
    emit fullScreenRequested(on);
}

void PageView::stop()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void PageView::load()
{
    stop();
    clearContent();

    m_errorString.clear();
    emit errorStringChanged();

    if (!m_url.isValid() || m_url.isEmpty()) {
        setStatus(Null);
        return;
    }

    setProgress(0.0);
    setStatus(Loading);

    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("QmlBrowser/1.0 (Qt %1)").arg(QLatin1String(QT_VERSION_STR)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // The server also sends no-store. AlwaysNetwork is the client half of that:
    // editing a .qml file and pressing F5 must not replay a cached copy.
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &PageView::onReplyFinished);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                setProgress(total > 0 ? qreal(received) / qreal(total) : 0.0);
            });
}

void PageView::onReplyFinished()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const QByteArray body = reply->readAll();
    const QVariant code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

    const int newStatus = code.isValid() ? code.toInt() : 0;
    if (newStatus != m_httpStatus) {
        m_httpStatus = newStatus;
        emit httpStatusChanged();
    }

    // A 404 from this server still carries a renderable QML body, so only treat
    // a transport failure with no payload as fatal.
    if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
        fail(QStringLiteral("%1\n\n%2")
                     .arg(reply->errorString(), m_url.toString()));
        return;
    }

    setProgress(1.0);
    instantiate(body);
}

void PageView::instantiate(const QByteArray &source)
{
    m_sourceText = QString::fromUtf8(source);
    emit sourceTextChanged();

    QQmlEngine *engine = qmlEngine(this);
    if (!engine) {
        fail(QStringLiteral("No QML engine is associated with this view."));
        return;
    }

    delete m_component;
    m_component = new QQmlComponent(engine, this);

    // Passing the document URL makes relative images, imports and qmldir
    // lookups resolve back to the server it came from.
    m_component->setData(source, m_url);

    if (m_component->isLoading()) {
        connect(m_component, &QQmlComponent::statusChanged, this,
                [this] { onComponentReady(); });
    } else {
        onComponentReady();
    }
}

void PageView::onComponentReady()
{
    if (!m_component || m_component->isLoading())
        return;

    if (m_component->isError()) {
        QStringList lines;
        const QList<QQmlError> errors = m_component->errors();
        lines.reserve(errors.size());
        for (const QQmlError &error : errors) {
            lines << QStringLiteral("line %1:%2  %3")
                             .arg(error.line())
                             .arg(error.column())
                             .arg(error.description());
        }
        fail(lines.join(QLatin1Char('\n')));
        return;
    }

    // A private context so the document can reach `browser` but cannot see (or
    // clobber) anything belonging to the browser chrome.
    auto *context = new QQmlContext(qmlContext(this), this);
    context->setContextProperty(QStringLiteral("browser"), this);

    QObject *object = m_component->beginCreate(context);
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        delete object;
        delete context;
        fail(QStringLiteral("The root object of this document is not an Item. "
                            "A page must have a visual root such as Item or Rectangle."));
        return;
    }

    // Size it before completeCreate() so Component.onCompleted already sees the
    // real viewport dimensions.
    item->setParentItem(this);
    item->setWidth(width());
    item->setHeight(height());
    m_component->completeCreate();

    context->setParent(item);
    item->setParent(this);
    m_content = item;

    QQmlProperty titleProperty(item, QStringLiteral("title"));
    if (titleProperty.isValid() && titleProperty.hasNotifySignal())
        titleProperty.connectNotifySignal(this, SLOT(refreshPageTitle()));
    refreshPageTitle();

    setStatus(Ready);
    emit loadFinished(true);
}

void PageView::refreshPageTitle()
{
    QString title;
    if (m_content) {
        const QVariant value = m_content->property("title");
        if (value.isValid())
            title = value.toString();
    }
    // Documents are not required to declare `title`. The file name is a better
    // tab label than a blank, and the host covers a URL that is only "/".
    if (title.isEmpty())
        title = m_url.fileName().isEmpty() ? m_url.host() : m_url.fileName();

    if (title != m_pageTitle) {
        m_pageTitle = title;
        emit pageTitleChanged();
    }
}

void PageView::fail(const QString &message)
{
    clearContent();

    qWarning().noquote() << "Failed to load" << m_url.toString() << '\n' << message;

    m_errorString = message;
    emit errorStringChanged();

    m_pageTitle = QStringLiteral("Problem loading page");
    emit pageTitleChanged();

    setProgress(1.0);
    setStatus(Error);
    emit loadFinished(false);
}

void PageView::clearContent()
{
    if (m_content) {
        m_content->setParentItem(nullptr);
        m_content->deleteLater();
        m_content = nullptr;
    }
    if (m_component) {
        m_component->deleteLater();
        m_component = nullptr;
    }
}

void PageView::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void PageView::setProgress(qreal progress)
{
    if (qFuzzyCompare(m_progress, progress))
        return;
    m_progress = progress;
    emit progressChanged();
}

void PageView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (m_content) {
        m_content->setWidth(newGeometry.width());
        m_content->setHeight(newGeometry.height());
    }
}
