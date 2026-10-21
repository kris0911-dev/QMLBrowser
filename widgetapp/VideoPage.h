#pragma once

#include <QImage>
#include <QObject>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QTimer;
class QToolButton;
QT_END_NAMESPACE

#ifdef WIDGETAPP_HAVE_MULTIMEDIA
QT_BEGIN_NAMESPACE
class QAudioOutput;
class QMediaPlayer;
class QVideoSink;
QT_END_NAMESPACE
#endif

// Paints the current video frame, letterboxed, with an optional caption.
class VideoSurface : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSurface(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void setCaption(const QString &caption);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QImage m_frame;
    QString m_caption;
};

// A generated test clip: every frame is a function of its timestamp, so it
// plays, pauses, seeks and changes speed like a real video without needing
// a codec or a file.
class SyntheticClip : public QObject
{
    Q_OBJECT

public:
    explicit SyntheticClip(QObject *parent = nullptr);

    static constexpr qint64 kDuration = 20000;
    static constexpr int kFps = 30;

    bool isPlaying() const;
    qint64 position() const { return m_position; }
    void play();
    void pause();
    void seek(qint64 ms);
    void setRate(qreal rate) { m_rate = rate; }
    void setLooping(bool on) { m_loop = on; }

    QImage render(qint64 ms) const;

signals:
    void frameReady(const QImage &frame);
    void positionChanged(qint64 ms);
    void playingChanged(bool playing);

private:
    void tick();

    QTimer *m_timer = nullptr;
    qint64 m_position = 0;
    qreal m_rate = 1;
    bool m_loop = true;
};

class VideoPage : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPage(QWidget *parent = nullptr);

    // Whether this build can decode video files (Qt Multimedia was found).
    static bool canPlayFiles();

    void openFile(const QString &path);
    void togglePlayback();
    void pause();
    void setRate(qreal rate);

signals:
    void message(const QString &text);

private:
    enum class Source { Clip, File };

    void play();
    void seek(qint64 ms);
    void selectSource(Source source);
    void updatePosition(qint64 ms);
    void updateDuration(qint64 ms);
    void updatePlaying(bool playing);
    qint64 duration() const;
    static QString timeText(qint64 ms);

    Source m_source = Source::Clip;
    VideoSurface *m_surface = nullptr;
    SyntheticClip *m_clip = nullptr;
    QToolButton *m_play = nullptr;
    QToolButton *m_stop = nullptr;
    QSlider *m_position = nullptr;
    QLabel *m_time = nullptr;
    QComboBox *m_rate = nullptr;
    QComboBox *m_sourceBox = nullptr;
    QCheckBox *m_loop = nullptr;
    QSlider *m_volume = nullptr;
    bool m_scrubbing = false;

#ifdef WIDGETAPP_HAVE_MULTIMEDIA
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoSink *m_sink = nullptr;
#endif
};
