#pragma once

#include <QMainWindow>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QLabel;
class QTabWidget;
QT_END_NAMESPACE

class ControlsPage;
class PaintPage;
class PicturePage;
class ScenePage;
class VectorPage;
class VideoPage;

// The widget application shown in the browser's app://widgets tab.
//
// It is an ordinary QMainWindow, with a menu bar, a tool bar, a status bar
// and one tab per kind of content: standard controls, freehand painting,
// raster pictures, vector graphics, video and a 3D scene. Nothing in it knows
// about the browser. QmlBrowserHost hosts it there (main.cpp), and without
// --channel it runs as a normal desktop window, which is handy for comparing
// the two.
class GalleryWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit GalleryWindow(bool hosted, QWidget *parent = nullptr);

    // Page names as they appear in the address: controls, paint, pictures,
    // vectors, video, 3d.
    static QStringList pageNames();
    QString pageName() const;
    void showPage(const QString &name);

signals:
    void pageChanged(const QString &name);

private:
    void buildMenus();
    void buildToolBar();
    void openPicture();
    void openVideo();
    void showAbout();
    void showMessage(const QString &text);

    bool m_hosted = false;
    QTabWidget *m_tabs = nullptr;
    ControlsPage *m_controls = nullptr;
    PaintPage *m_paint = nullptr;
    PicturePage *m_pictures = nullptr;
    VectorPage *m_vectors = nullptr;
    VideoPage *m_video = nullptr;
    ScenePage *m_scene = nullptr;
    QLabel *m_platform = nullptr;
};
