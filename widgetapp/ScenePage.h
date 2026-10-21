#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector3D>
#include <QWidget>

#include <memory>

QT_BEGIN_NAMESPACE
class QLabel;
class QOpenGLShaderProgram;
class QStackedWidget;
class QTimer;
QT_END_NAMESPACE

// A lit 3D model in a QOpenGLWidget. Drag to orbit, wheel to zoom, double
// click to reset the view.
//
// Only OpenGL 2.1 / OpenGL ES 2.0 features are used (GLSL 1.10, vertex
// buffers, no vertex array objects), so the same code runs on the macOS
// legacy context, on desktop OpenGL on Windows, and on Qt's software OpenGL.
class SceneView : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    enum Shape { TorusKnot, Torus, Sphere, Cube };

    explicit SceneView(QWidget *parent = nullptr);
    ~SceneView() override;

    void setShape(Shape shape);
    void setColor(const QColor &color);
    QColor color() const { return m_color; }
    void setWireframe(bool on);
    void setSpinning(bool on);
    void setSpinSpeed(int degreesPerSecond);
    void resetView();

    // "OpenGL 2.1 Metal - 83, Apple M1", once a context exists.
    QString rendererInfo() const { return m_rendererInfo; }

signals:
    void initialized(bool ok);
    void framesPerSecond(int fps);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Mesh {
        std::unique_ptr<QOpenGLBuffer> vertices;
        std::unique_ptr<QOpenGLBuffer> triangles;
        std::unique_ptr<QOpenGLBuffer> edges;
        int triangleIndexCount = 0;
        int edgeIndexCount = 0;
    };

    void buildMesh(Mesh &mesh, Shape shape);
    void drawMesh(const Mesh &mesh, const QMatrix4x4 &model, const QMatrix4x4 &viewProjection,
                  const QColor &color, bool lit, bool lines);
    void cleanup();

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    Mesh m_model;
    Mesh m_ground;
    Shape m_shape = TorusKnot;
    bool m_meshDirty = true;

    QColor m_color = QColor(QStringLiteral("#4f8cff"));
    bool m_wireframe = false;
    bool m_spinning = true;
    float m_spinSpeed = 40;
    float m_spin = 0;
    float m_yaw = -30;
    float m_pitch = 22;
    float m_distance = 5.2f;
    QPointF m_lastPos;
    QMatrix4x4 m_projection;

    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastTick = 0;
    int m_frames = 0;
    qint64 m_fpsSince = 0;
    QString m_rendererInfo;
};

// The 3D page: the view above, its controls, and a note when OpenGL is not
// available (the app hosted in Software mode).
class ScenePage : public QWidget
{
    Q_OBJECT

public:
    explicit ScenePage(QWidget *parent = nullptr);

signals:
    void message(const QString &text);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void chooseColor();
    void showUnavailable();

    QStackedWidget *m_stack = nullptr;
    SceneView *m_view = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_fps = nullptr;
    QLabel *m_unavailable = nullptr;
};
