#include "VideoPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef WIDGETAPP_HAVE_MULTIMEDIA
#  include <QAudioOutput>
#  include <QMediaPlayer>
#  include <QVideoFrame>
#  include <QVideoSink>
#endif

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kClipWidth = 960;
constexpr int kClipHeight = 540;

} // namespace

// ------------------------------------------------------------ VideoSurface

VideoSurface::VideoSurface(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(240, 135);
}

void VideoSurface::setFrame(const QImage &frame)
{
    m_frame = frame;
    update();
}

void VideoSurface::setCaption(const QString &caption)
{
    if (caption == m_caption)
        return;
    m_caption = caption;
    update();
}

void VideoSurface::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_frame.isNull()) {
        const QSize fitted = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2),
                           fitted);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, m_frame);
    }
    if (!m_caption.isEmpty()) {
        QFont font = p.font();
        font.setPixelSize(14);
        p.setFont(font);
        const QRect box(12, height() - 40, p.fontMetrics().horizontalAdvance(m_caption) + 24, 28);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, 8, 8);
        p.setPen(QColor(QStringLiteral("#e6ecf9")));
        p.drawText(box, Qt::AlignCenter, m_caption);
    }
}

void VideoSurface::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
}

// ----------------------------------------------------------- SyntheticClip

SyntheticClip::SyntheticClip(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(1000 / kFps);
    connect(m_timer, &QTimer::timeout, this, &SyntheticClip::tick);
}

bool SyntheticClip::isPlaying() const
{
    return m_timer->isActive();
}

void SyntheticClip::play()
{
    if (isPlaying())
        return;
    if (m_position >= kDuration)
        m_position = 0;
    m_timer->start();
    emit playingChanged(true);
}

void SyntheticClip::pause()
{
    if (!isPlaying())
        return;
    m_timer->stop();
    emit playingChanged(false);
}

void SyntheticClip::seek(qint64 ms)
{
    m_position = qBound<qint64>(0, ms, kDuration);
    emit positionChanged(m_position);
    emit frameReady(render(m_position));
}

void SyntheticClip::tick()
{
    m_position += qint64(1000.0 / kFps * m_rate);
    if (m_position >= kDuration) {
        if (m_loop) {
            m_position %= kDuration;
        } else {
            m_position = kDuration;
            pause();
        }
    }
    emit positionChanged(m_position);
    emit frameReady(render(m_position));
}

QImage SyntheticClip::render(qint64 ms) const
{
    const double t = ms / 1000.0;
    const int frame = int(ms * kFps / 1000);

    QImage image(kClipWidth, kClipHeight, QImage::Format_RGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);

    // Background: a slow hue drift.
    QLinearGradient sky(0, 0, kClipWidth, kClipHeight);
    sky.setColorAt(0, QColor::fromHsvF(std::fmod(0.6 + t * 0.01, 1.0), 0.55, 0.28));
    sky.setColorAt(1, QColor::fromHsvF(std::fmod(0.8 + t * 0.01, 1.0), 0.6, 0.12));
    p.fillRect(image.rect(), sky);

    // Color bars that slide sideways, the classic test pattern.
    const QList<QColor> bars = { QColor(192, 192, 192), QColor(192, 192, 0), QColor(0, 192, 192),
                                 QColor(0, 192, 0), QColor(192, 0, 192), QColor(192, 0, 0),
                                 QColor(0, 0, 192) };
    const qreal barWidth = kClipWidth / 7.0;
    const qreal shift = std::fmod(t * 60, kClipWidth);
    for (int i = 0; i < 14; ++i) {
        const qreal x = std::fmod(i * barWidth + shift, kClipWidth * 2.0) - barWidth;
        p.fillRect(QRectF(x, 0, barWidth + 1, 70), bars.at(i % 7));
    }

    // Bouncing ball with a fading trail.
    for (int k = 8; k >= 0; --k) {
        const double tt = t - k * 0.03;
        const double bx = 120 + std::fabs(std::fmod(tt * 260, 2 * (kClipWidth - 240)) - (kClipWidth - 240));
        const double by = 470 - std::fabs(std::sin(tt * 2.6)) * 320;
        const int alpha = k == 0 ? 255 : 90 - k * 10;
        QRadialGradient ball(QPointF(bx - 10, by - 12), 44);
        ball.setColorAt(0, QColor(255, 240, 210, alpha));
        ball.setColorAt(1, QColor(240, 120, 60, alpha));
        p.setPen(Qt::NoPen);
        p.setBrush(ball);
        p.drawEllipse(QPointF(bx, by), 34 - k * 1.5, 34 - k * 1.5);
    }

    // A clock face whose hand sweeps once every five seconds.
    const QPointF centre(780, 300);
    p.setPen(QPen(QColor(255, 255, 255, 180), 3));
    p.setBrush(QColor(0, 0, 0, 90));
    p.drawEllipse(centre, 110, 110);
    for (int i = 0; i < 60; ++i) {
        const double a = i * 2 * kPi / 60;
        const double inner = i % 5 == 0 ? 92 : 100;
        p.setPen(QPen(QColor(255, 255, 255, i % 5 == 0 ? 220 : 110), i % 5 == 0 ? 3 : 1.5));
        p.drawLine(centre + QPointF(std::sin(a) * inner, -std::cos(a) * inner),
                   centre + QPointF(std::sin(a) * 106, -std::cos(a) * 106));
    }
    const double sweep = std::fmod(t / 5.0, 1.0) * 2 * kPi;
    QPainterPath wedge;
    wedge.moveTo(centre);
    wedge.arcTo(QRectF(centre - QPointF(100, 100), QSizeF(200, 200)), 90, -sweep * 180 / kPi);
    wedge.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(138, 180, 255, 70));
    p.drawPath(wedge);
    p.setPen(QPen(QColor(QStringLiteral("#ff8095")), 4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(centre, centre + QPointF(std::sin(sweep) * 96, -std::cos(sweep) * 96));

    // Timecode and frame counter, so dropped or repeated frames are visible.
    QFont big;
    big.setPixelSize(54);
    big.setBold(true);
    big.setStyleHint(QFont::Monospace);
    big.setFamily(QStringLiteral("Menlo"));
    p.setFont(big);
    const int minutes = int(ms / 60000);
    const int seconds = int((ms / 1000) % 60);
    const int frames = frame % kFps;
    const QString code = QStringLiteral("%1:%2:%3")
                                 .arg(minutes, 2, 10, QLatin1Char('0'))
                                 .arg(seconds, 2, 10, QLatin1Char('0'))
                                 .arg(frames, 2, 10, QLatin1Char('0'));
    p.setPen(QColor(0, 0, 0, 140));
    p.drawText(QRectF(42, 112, 500, 70), Qt::AlignLeft | Qt::AlignVCenter, code);
    p.setPen(Qt::white);
    p.drawText(QRectF(40, 110, 500, 70), Qt::AlignLeft | Qt::AlignVCenter, code);

    QFont small;
    small.setPixelSize(18);
    p.setFont(small);
    p.setPen(QColor(255, 255, 255, 200));
    p.drawText(QRectF(42, 180, 500, 30), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("frame %1  ·  %2 fps  ·  %3×%4")
                       .arg(frame)
                       .arg(kFps)
                       .arg(kClipWidth)
                       .arg(kClipHeight));

    // Ticker along the bottom.
    QFont ticker;
    ticker.setPixelSize(20);
    p.setFont(ticker);
    const QString line = QStringLiteral(
            "QPainter → QImage → VideoSurface → QWidget::render → shared memory "
            "→ QmlBrowser → Qt Quick scene graph      ");
    const int lineWidth = p.fontMetrics().horizontalAdvance(line);
    p.fillRect(QRectF(0, kClipHeight - 44, kClipWidth, 44), QColor(0, 0, 0, 150));
    p.setPen(QColor(QStringLiteral("#7ce3b0")));
    const qreal offset = std::fmod(t * 120, lineWidth);
    for (qreal x = -offset; x < kClipWidth; x += lineWidth)
        p.drawText(QPointF(x, kClipHeight - 15), line);

    return image;
}

// --------------------------------------------------------------- VideoPage

VideoPage::VideoPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);

    m_surface = new VideoSurface;
    layout->addWidget(m_surface, 1);

    auto *controls = new QHBoxLayout;
    m_play = new QToolButton;
    m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_play->setToolTip(QStringLiteral("Play / pause"));
    m_stop = new QToolButton;
    m_stop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stop->setToolTip(QStringLiteral("Stop"));
    controls->addWidget(m_play);
    controls->addWidget(m_stop);

    m_position = new QSlider(Qt::Horizontal);
    m_position->setRange(0, int(SyntheticClip::kDuration));
    controls->addWidget(m_position, 1);

    m_time = new QLabel;
    m_time->setMinimumWidth(110);
    m_time->setAlignment(Qt::AlignCenter);
    controls->addWidget(m_time);

    m_rate = new QComboBox;
    for (const qreal rate : { 0.25, 0.5, 1.0, 1.5, 2.0 })
        m_rate->addItem(QStringLiteral("%1×").arg(rate), rate);
    m_rate->setCurrentIndex(2);
    controls->addWidget(m_rate);

    m_loop = new QCheckBox(QStringLiteral("Loop"));
    m_loop->setChecked(true);
    controls->addWidget(m_loop);
    layout->addLayout(controls);

    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new QLabel(QStringLiteral("Source")));
    m_sourceBox = new QComboBox;
    m_sourceBox->addItem(QStringLiteral("Generated test clip"));
    sourceRow->addWidget(m_sourceBox);
    sourceRow->addSpacing(16);
    auto *volumeLabel = new QLabel(QStringLiteral("Volume"));
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setValue(60);
    m_volume->setFixedWidth(120);
    sourceRow->addWidget(volumeLabel);
    sourceRow->addWidget(m_volume);
    sourceRow->addStretch();
    auto *note = new QLabel;
    note->setForegroundRole(QPalette::PlaceholderText);
    sourceRow->addWidget(note);
    layout->addLayout(sourceRow);

    m_clip = new SyntheticClip(this);
    connect(m_clip, &SyntheticClip::frameReady, this, [this](const QImage &frame) {
        if (m_source == Source::Clip)
            m_surface->setFrame(frame);
    });
    connect(m_clip, &SyntheticClip::positionChanged, this, [this](qint64 ms) {
        if (m_source == Source::Clip)
            updatePosition(ms);
    });
    connect(m_clip, &SyntheticClip::playingChanged, this, [this](bool playing) {
        if (m_source == Source::Clip)
            updatePlaying(playing);
    });

#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(m_volume->value() / 100.0f);
    m_player->setAudioOutput(m_audio);
    // Frames arrive in a QVideoSink rather than a QVideoWidget. QVideoWidget
    // draws through its own native surface, which QWidget::render cannot
    // capture; a sink hands over each frame as an image this page paints.
    m_sink = new QVideoSink(this);
    m_player->setVideoSink(m_sink);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (m_source == Source::File && frame.isValid())
            m_surface->setFrame(frame.toImage());
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (m_source == Source::File)
            updatePosition(ms);
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        if (m_source == Source::File)
            updateDuration(ms);
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (m_source == Source::File)
                    updatePlaying(state == QMediaPlayer::PlayingState);
            });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &text) {
                emit message(QStringLiteral("Video error: ") + text);
            });
    connect(m_volume, &QSlider::valueChanged, this,
            [this](int value) { m_audio->setVolume(value / 100.0f); });
    note->setText(QStringLiteral("File › Open video plays a file with Qt Multimedia."));
#else
    volumeLabel->setEnabled(false);
    m_volume->setEnabled(false);
    m_volume->setToolTip(QStringLiteral("The generated clip has no sound."));
    note->setText(QStringLiteral("Built without Qt Multimedia: the generated clip is the only source."));
#endif

    connect(m_sourceBox, &QComboBox::activated, this,
            [this](int index) { selectSource(index == 0 ? Source::Clip : Source::File); });
    connect(m_surface, &VideoSurface::clicked, this, &VideoPage::togglePlayback);
    connect(m_play, &QToolButton::clicked, this, &VideoPage::togglePlayback);
    connect(m_stop, &QToolButton::clicked, this, [this] {
        pause();
        seek(0);
    });
    connect(m_position, &QSlider::sliderPressed, this, [this] { m_scrubbing = true; });
    connect(m_position, &QSlider::sliderReleased, this, [this] {
        m_scrubbing = false;
        seek(m_position->value());
    });
    connect(m_position, &QSlider::valueChanged, this, [this](int value) {
        // Dragging the handle scrubs; clicking the groove jumps.
        if (m_scrubbing || !m_position->isSliderDown())
            seek(value);
    });
    connect(m_rate, &QComboBox::currentIndexChanged, this,
            [this] { setRate(m_rate->currentData().toReal()); });
    connect(m_loop, &QCheckBox::toggled, this, [this](bool on) {
        m_clip->setLooping(on);
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
        m_player->setLoops(on ? QMediaPlayer::Infinite : QMediaPlayer::Once);
#endif
    });

    updateDuration(SyntheticClip::kDuration);
    m_clip->seek(0);
    m_surface->setCaption(QStringLiteral("Click to play"));
}

bool VideoPage::canPlayFiles()
{
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

QString VideoPage::timeText(qint64 ms)
{
    return QStringLiteral("%1:%2.%3")
            .arg(ms / 60000)
            .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
            .arg((ms / 100) % 10);
}

qint64 VideoPage::duration() const
{
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    if (m_source == Source::File)
        return m_player->duration();
#endif
    return SyntheticClip::kDuration;
}

void VideoPage::updatePosition(qint64 ms)
{
    if (!m_position->isSliderDown()) {
        QSignalBlocker block(m_position);
        m_position->setValue(int(ms));
    }
    m_time->setText(timeText(ms) + QStringLiteral(" / ") + timeText(duration()));
}

void VideoPage::updateDuration(qint64 ms)
{
    QSignalBlocker block(m_position);
    m_position->setRange(0, int(qMax<qint64>(ms, 1)));
    m_time->setText(timeText(m_position->value()) + QStringLiteral(" / ") + timeText(ms));
}

void VideoPage::updatePlaying(bool playing)
{
    m_play->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    m_surface->setCaption(playing ? QString() : QStringLiteral("Paused · click to play"));
}

void VideoPage::play()
{
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    if (m_source == Source::File) {
        m_player->play();
        return;
    }
#endif
    m_clip->play();
}

void VideoPage::pause()
{
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    if (m_source == Source::File) {
        m_player->pause();
        return;
    }
#endif
    m_clip->pause();
}

void VideoPage::togglePlayback()
{
    bool playing = m_clip->isPlaying();
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    if (m_source == Source::File)
        playing = m_player->playbackState() == QMediaPlayer::PlayingState;
#endif
    if (playing)
        pause();
    else
        play();
}

void VideoPage::seek(qint64 ms)
{
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    if (m_source == Source::File) {
        m_player->setPosition(ms);
        return;
    }
#endif
    m_clip->seek(ms);
}

void VideoPage::setRate(qreal rate)
{
    m_clip->setRate(rate);
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    m_player->setPlaybackRate(rate);
#endif
    const int index = m_rate->findData(rate);
    if (index >= 0 && index != m_rate->currentIndex()) {
        QSignalBlocker block(m_rate);
        m_rate->setCurrentIndex(index);
    }
    emit message(QStringLiteral("Playback speed %1×").arg(rate));
}

void VideoPage::selectSource(Source source)
{
    if (source == m_source)
        return;
    pause();
    m_source = source;
    m_sourceBox->setCurrentIndex(source == Source::Clip ? 0 : 1);
    updateDuration(duration());
    if (source == Source::Clip)
        m_clip->seek(m_clip->position());
    updatePlaying(false);
}

void VideoPage::openFile(const QString &path)
{
#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    if (m_sourceBox->count() < 2)
        m_sourceBox->addItem(QFileInfo(path).fileName());
    else
        m_sourceBox->setItemText(1, QFileInfo(path).fileName());
    selectSource(Source::File);
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->setLoops(m_loop->isChecked() ? QMediaPlayer::Infinite : QMediaPlayer::Once);
    m_player->play();
    emit message(QStringLiteral("Playing ") + QFileInfo(path).fileName());
#else
    Q_UNUSED(path);
    emit message(QStringLiteral("This build has no Qt Multimedia, so it cannot decode video files."));
#endif
}
