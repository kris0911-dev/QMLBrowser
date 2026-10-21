#include "GalleryWindow.h"

#include "ControlsPage.h"
#include "PaintPage.h"
#include "PicturePage.h"
#include "ScenePage.h"
#include "VectorPage.h"
#include "VideoPage.h"

#include <QmlBrowserHost.h>

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>

GalleryWindow::GalleryWindow(bool hosted, QWidget *parent)
    : QMainWindow(parent)
    , m_hosted(hosted)
{
    setWindowTitle(QStringLiteral("Qt Widgets Gallery"));
    // Pages scroll instead of forcing a minimum size, so the window always
    // fits the tab it is given.
    setMinimumSize(320, 240);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_controls = new ControlsPage;
    m_paint = new PaintPage;
    m_pictures = new PicturePage;
    m_vectors = new VectorPage;
    m_video = new VideoPage;
    m_scene = new ScenePage;
    m_tabs->addTab(m_controls, QStringLiteral("Controls"));
    m_tabs->addTab(m_paint, QStringLiteral("Paint"));
    m_tabs->addTab(m_pictures, QStringLiteral("Pictures"));
    m_tabs->addTab(m_vectors, QStringLiteral("Vectors"));
    m_tabs->addTab(m_video, QStringLiteral("Video"));
    m_tabs->addTab(m_scene, QStringLiteral("3D"));
    setCentralWidget(m_tabs);

    connect(m_controls, &ControlsPage::message, this, &GalleryWindow::showMessage);
    connect(m_paint, &PaintPage::message, this, &GalleryWindow::showMessage);
    connect(m_pictures, &PicturePage::message, this, &GalleryWindow::showMessage);
    connect(m_vectors, &VectorPage::message, this, &GalleryWindow::showMessage);
    connect(m_video, &VideoPage::message, this, &GalleryWindow::showMessage);
    connect(m_scene, &ScenePage::message, this, &GalleryWindow::showMessage);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        // The video keeps its position but stops running on a hidden page.
        if (m_tabs->widget(index) != m_video)
            m_video->pause();
        emit pageChanged(pageName());
    });

    buildMenus();
    buildToolBar();

    m_platform = new QLabel(QStringLiteral("Qt %1  ·  %2 platform%3")
                                    .arg(QLatin1String(qVersion()),
                                         QGuiApplication::platformName(),
                                         hosted ? QmlBrowserHost::graphics() == QmlBrowserHost::Graphics::Gpu
                                                          ? QStringLiteral("  ·  hosted in QmlBrowser, GPU mode")
                                                          : QStringLiteral("  ·  hosted in QmlBrowser, software mode")
                                                : QString()));
    m_platform->setContentsMargins(8, 0, 8, 0);
    statusBar()->addPermanentWidget(m_platform);
    // A tab is sized by the browser; a grip in its corner would do nothing.
    statusBar()->setSizeGripEnabled(!hosted);
    statusBar()->showMessage(QStringLiteral("Ready"));
}

QStringList GalleryWindow::pageNames()
{
    return { QStringLiteral("controls"), QStringLiteral("paint"), QStringLiteral("pictures"),
             QStringLiteral("vectors"), QStringLiteral("video"), QStringLiteral("3d") };
}

QString GalleryWindow::pageName() const
{
    return pageNames().value(m_tabs->currentIndex());
}

void GalleryWindow::showPage(const QString &name)
{
    const int index = int(pageNames().indexOf(name.toLower()));
    if (index >= 0 && index != m_tabs->currentIndex())
        m_tabs->setCurrentIndex(index);
}

void GalleryWindow::showMessage(const QString &text)
{
    statusBar()->showMessage(text, 6000);
}

void GalleryWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *openPicture = file->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                           QStringLiteral("Open &picture…"), this,
                                           &GalleryWindow::openPicture);
    openPicture->setShortcut(QKeySequence::Open);
    QAction *openVideo = file->addAction(style()->standardIcon(QStyle::SP_MediaPlay),
                                         QStringLiteral("Open &video…"), this,
                                         &GalleryWindow::openVideo);
    openVideo->setEnabled(VideoPage::canPlayFiles());
    file->addSeparator();
    file->addAction(QStringLiteral("&Clear painting"), m_paint, &PaintPage::clear);
    if (!m_hosted) {
        // In a tab the browser decides when this process ends.
        file->addSeparator();
        QAction *quit = file->addAction(QStringLiteral("&Quit"), qApp, &QApplication::quit);
        quit->setShortcut(QKeySequence::Quit);
    }

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    const QStringList labels = { QStringLiteral("&Controls"), QStringLiteral("&Paint"),
                                 QStringLiteral("P&ictures"), QStringLiteral("&Vectors"),
                                 QStringLiteral("Vi&deo"), QStringLiteral("&3D") };
    for (int i = 0; i < labels.size(); ++i) {
        QAction *action = view->addAction(labels.at(i), this,
                                          [this, i] { m_tabs->setCurrentIndex(i); });
        action->setShortcut(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_1 + i)));
    }

    QMenu *video = menuBar()->addMenu(QStringLiteral("&Playback"));
    video->addAction(QStringLiteral("&Play / pause"), this, [this] {
        showPage(QStringLiteral("video"));
        m_video->togglePlayback();
    });
    QMenu *speed = video->addMenu(QStringLiteral("&Speed"));
    for (const qreal rate : { 0.5, 1.0, 1.5, 2.0 }) {
        speed->addAction(QStringLiteral("%1×").arg(rate), this,
                         [this, rate] { m_video->setRate(rate); });
    }

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(QStringLiteral("&About this app…"), this, &GalleryWindow::showAbout);
    help->addAction(QStringLiteral("About &Qt…"), qApp, &QApplication::aboutQt);
}

void GalleryWindow::buildToolBar()
{
    QToolBar *bar = addToolBar(QStringLiteral("Main"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setIconSize(QSize(16, 16));

    bar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("Open picture"),
                   this, &GalleryWindow::openPicture)
            ->setToolTip(QStringLiteral("Load an image file into the Pictures page"));
    bar->addAction(style()->standardIcon(QStyle::SP_DialogResetButton), QStringLiteral("Clear painting"),
                   this, [this] {
                       m_paint->clear();
                       showPage(QStringLiteral("paint"));
                   })
            ->setToolTip(QStringLiteral("Wipe the Paint canvas"));
    bar->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Play / pause"), this,
                   [this] {
                       showPage(QStringLiteral("video"));
                       m_video->togglePlayback();
                   })
            ->setToolTip(QStringLiteral("Start or pause the Video page"));
    bar->addSeparator();
    bar->addAction(style()->standardIcon(QStyle::SP_MessageBoxInformation), QStringLiteral("About"),
                   this, &GalleryWindow::showAbout)
            ->setToolTip(QStringLiteral("Opens a modal dialog"));
}

void GalleryWindow::openPicture()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open picture"), start,
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg);;All files (*)"));
    if (path.isEmpty())
        return;
    showPage(QStringLiteral("pictures"));
    m_pictures->openFile(path);
}

void GalleryWindow::openVideo()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open video"), start,
            QStringLiteral("Videos (*.mp4 *.mov *.m4v *.mkv *.webm *.avi);;All files (*)"));
    if (path.isEmpty())
        return;
    showPage(QStringLiteral("video"));
    m_video->openFile(path);
}

void GalleryWindow::showAbout()
{
    QMessageBox::about(
            this, QStringLiteral("About Qt Widgets Gallery"),
            QStringLiteral("<h3>Qt Widgets Gallery</h3>"
                           "<p>A plain QMainWindow application. In QmlBrowser it runs in its own "
                           "process on Qt's offscreen platform and paints into shared memory; "
                           "the browser draws the frames and sends input back over the same "
                           "socket protocol a QML tab uses.</p>"
                           "<p>This dialog is a separate top-level window. It is composited "
                           "into the tab, and it is modal: the window behind it ignores clicks "
                           "until it closes.</p>"));
}
