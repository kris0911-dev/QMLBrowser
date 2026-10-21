#include "ScenePage.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979f;

// GLSL 1.10 / GLSL ES 1.00. On desktop OpenGL, QOpenGLShader defines the
// precision qualifiers away.
const char *kVertexShader = R"glsl(
attribute highp vec3 position;
attribute highp vec3 normal;
uniform highp mat4 mvp;
uniform highp mat4 model;
uniform mediump mat3 normalMatrix;
varying highp vec3 worldPos;
varying mediump vec3 worldNormal;
void main()
{
    worldPos = (model * vec4(position, 1.0)).xyz;
    worldNormal = normalMatrix * normal;
    gl_Position = mvp * vec4(position, 1.0);
}
)glsl";

const char *kFragmentShader = R"glsl(
uniform mediump vec4 color;
uniform mediump float lit;
uniform highp vec3 eye;
varying highp vec3 worldPos;
varying mediump vec3 worldNormal;
void main()
{
    if (lit < 0.5) {
        // Grid lines fade out with distance from the centre.
        mediump float fade = clamp(1.0 - length(worldPos.xz) / 5.5, 0.0, 1.0);
        gl_FragColor = vec4(color.rgb, color.a * fade);
        return;
    }
    mediump vec3 n = normalize(worldNormal);
    if (!gl_FrontFacing)
        n = -n;
    mediump vec3 v = normalize(eye - worldPos);
    mediump vec3 key = normalize(vec3(0.6, 0.9, 0.5));
    mediump vec3 fill = normalize(vec3(-0.7, 0.25, -0.6));
    mediump vec3 sky = mix(vec3(0.10, 0.09, 0.08), vec3(0.20, 0.24, 0.32), n.y * 0.5 + 0.5);
    mediump float diffuse = max(dot(n, key), 0.0) * 0.85 + max(dot(n, fill), 0.0) * 0.3;
    mediump float specular = pow(max(dot(n, normalize(key + v)), 0.0), 48.0) * 0.55;
    mediump float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.35;
    mediump vec3 rgb = color.rgb * (diffuse + sky) + vec3(specular)
            + rim * vec3(0.45, 0.65, 1.0);
    gl_FragColor = vec4(rgb, color.a);
}
)glsl";

struct Vertex {
    QVector3D position;
    QVector3D normal;
};

// A tube of `around` segments along a closed curve sampled `along` times.
// The cross-section is carried along by parallel transport: each ring's
// normal is the previous one with the change of direction taken out, so the
// tube neither twists nor pinches where the curve straightens.
void tube(QList<Vertex> &vertices, QList<quint16> &indices, int along, int around, float radius,
          const std::function<QVector3D(float)> &curve)
{
    QList<QVector3D> points;
    QList<QVector3D> tangents;
    for (int i = 0; i <= along; ++i) {
        const float t = 2 * kPi * i / along;
        const float step = 2 * kPi / along;
        points.append(curve(t));
        tangents.append((curve(t + step * 0.5f) - curve(t - step * 0.5f)).normalized());
    }

    // Any direction across the first tangent to start with.
    QVector3D normal = QVector3D::crossProduct(tangents.first(), QVector3D(0, 1, 0));
    if (normal.lengthSquared() < 1e-4f)
        normal = QVector3D::crossProduct(tangents.first(), QVector3D(1, 0, 0));
    normal.normalize();

    for (int i = 0; i <= along; ++i) {
        const QVector3D &tangent = tangents.at(i);
        normal = (normal - tangent * QVector3D::dotProduct(normal, tangent)).normalized();
        const QVector3D side = QVector3D::crossProduct(tangent, normal);
        for (int j = 0; j <= around; ++j) {
            const float a = 2 * kPi * j / around;
            const QVector3D n = normal * std::cos(a) + side * std::sin(a);
            vertices.append({ points.at(i) + n * radius, n });
        }
    }
    // The last ring is the first one again, give or take the twist that
    // transport accumulates around the loop. Snap it closed so no seam shows.
    const int last = along * (around + 1);
    const QVector3D endNormal = normal;
    float best = -2;
    int shift = 0;
    for (int j = 0; j <= around; ++j) {
        const float d = QVector3D::dotProduct(vertices.at(j).normal, endNormal);
        if (d > best) {
            best = d;
            shift = j;
        }
    }
    for (int j = 0; j <= around; ++j)
        vertices[last + j] = vertices.at((j + shift) % around);

    for (int i = 0; i < along; ++i) {
        for (int j = 0; j < around; ++j) {
            const auto a = quint16(i * (around + 1) + j);
            const auto b = quint16(a + around + 1);
            indices << a << b << quint16(a + 1) << quint16(a + 1) << b << quint16(b + 1);
        }
    }
}

void sphere(QList<Vertex> &vertices, QList<quint16> &indices, float radius)
{
    const int rings = 32;
    const int segments = 64;
    for (int i = 0; i <= rings; ++i) {
        const float phi = kPi * i / rings;
        for (int j = 0; j <= segments; ++j) {
            const float theta = 2 * kPi * j / segments;
            const QVector3D n(std::sin(phi) * std::cos(theta), std::cos(phi),
                              std::sin(phi) * std::sin(theta));
            vertices.append({ n * radius, n });
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const auto a = quint16(i * (segments + 1) + j);
            const auto b = quint16(a + segments + 1);
            indices << a << quint16(a + 1) << b << quint16(a + 1) << quint16(b + 1) << b;
        }
    }
}

void cube(QList<Vertex> &vertices, QList<quint16> &indices, float half)
{
    const QVector3D normals[] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                                  { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const QVector3D &n : normals) {
        // Two axes across the face.
        const QVector3D u = std::abs(n.y()) > 0.5f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
        const QVector3D w = QVector3D::crossProduct(n, u);
        const auto base = quint16(vertices.size());
        for (const auto &corner : { QPointF(-1, -1), QPointF(1, -1), QPointF(1, 1), QPointF(-1, 1) }) {
            const QVector3D p = (n + u * float(corner.x()) + w * float(corner.y())) * half;
            vertices.append({ p, n });
        }
        indices << base << quint16(base + 1) << quint16(base + 2)
                << base << quint16(base + 2) << quint16(base + 3);
    }
}

// Each triangle edge once, for the wireframe.
QList<quint16> edgesOf(const QList<quint16> &triangles)
{
    QSet<quint32> seen;
    QList<quint16> edges;
    for (qsizetype i = 0; i + 2 < triangles.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            quint16 a = triangles.at(i + k);
            quint16 b = triangles.at(i + (k + 1) % 3);
            if (a > b)
                std::swap(a, b);
            const quint32 key = (quint32(a) << 16) | b;
            if (seen.contains(key))
                continue;
            seen.insert(key);
            edges << a << b;
        }
    }
    return edges;
}

std::unique_ptr<QOpenGLBuffer> upload(QOpenGLBuffer::Type type, const void *data, int bytes)
{
    auto buffer = std::make_unique<QOpenGLBuffer>(type);
    buffer->create();
    buffer->bind();
    buffer->allocate(data, bytes);
    buffer->release();
    return buffer;
}

} // namespace

// ------------------------------------------------------------------ view

SceneView::SceneView(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setDepthBufferSize(24);
    format.setSamples(4);
    setFormat(format);
    setMinimumSize(240, 200);

    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this] {
        const qint64 now = m_clock.elapsed();
        m_spin = std::fmod(m_spin + m_spinSpeed * float(now - m_lastTick) / 1000.0f, 360.0f);
        m_lastTick = now;
        update();
    });
    m_clock.start();
}

SceneView::~SceneView()
{
    makeCurrent();
    cleanup();
    doneCurrent();
}

void SceneView::cleanup()
{
    m_program.reset();
    m_model = Mesh();
    m_ground = Mesh();
}

void SceneView::setShape(Shape shape)
{
    if (shape == m_shape)
        return;
    m_shape = shape;
    m_meshDirty = true;
    update();
}

void SceneView::setColor(const QColor &color)
{
    m_color = color;
    update();
}

void SceneView::setWireframe(bool on)
{
    m_wireframe = on;
    update();
}

void SceneView::setSpinning(bool on)
{
    m_spinning = on;
    m_lastTick = m_clock.elapsed();
    if (on && isVisible())
        m_timer->start();
    else
        m_timer->stop();
}

void SceneView::setSpinSpeed(int degreesPerSecond)
{
    m_spinSpeed = float(degreesPerSecond);
}

void SceneView::resetView()
{
    m_yaw = -30;
    m_pitch = 22;
    m_distance = 5.2f;
    update();
}

void SceneView::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    m_lastTick = m_clock.elapsed();
    if (m_spinning)
        m_timer->start();
}

void SceneView::hideEvent(QHideEvent *event)
{
    m_timer->stop();
    QOpenGLWidget::hideEvent(event);
}

void SceneView::initializeGL()
{
    initializeOpenGLFunctions();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        makeCurrent();
        cleanup();
        doneCurrent();
    });

    m_rendererInfo = QStringLiteral("%1 %2  ·  %3")
                             .arg(context()->isOpenGLES() ? QStringLiteral("OpenGL ES")
                                                          : QStringLiteral("OpenGL"),
                                  QString::fromLatin1(reinterpret_cast<const char *>(
                                          glGetString(GL_VERSION))),
                                  QString::fromLatin1(reinterpret_cast<const char *>(
                                          glGetString(GL_RENDERER))));

    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->bindAttributeLocation("position", 0);
    m_program->bindAttributeLocation("normal", 1);
    if (!m_program->link()) {
        qWarning("SceneView: %s", qPrintable(m_program->log()));
        emit initialized(false);
        return;
    }

    // The ground: grid lines on a plane below the model.
    QList<Vertex> grid;
    QList<quint16> lines;
    const float extent = 5;
    const int steps = 20;
    for (int i = 0; i <= steps; ++i) {
        const float x = -extent + 2 * extent * i / steps;
        for (const QVector3D &p : { QVector3D(x, -1.7f, -extent), QVector3D(x, -1.7f, extent),
                                     QVector3D(-extent, -1.7f, x), QVector3D(extent, -1.7f, x) }) {
            lines << quint16(grid.size());
            grid.append({ p, QVector3D(0, 1, 0) });
        }
    }
    m_ground.vertices = upload(QOpenGLBuffer::VertexBuffer, grid.constData(),
                               int(grid.size() * sizeof(Vertex)));
    m_ground.edges = upload(QOpenGLBuffer::IndexBuffer, lines.constData(),
                            int(lines.size() * sizeof(quint16)));
    m_ground.edgeIndexCount = int(lines.size());

    m_meshDirty = true;
    emit initialized(true);
}

void SceneView::buildMesh(Mesh &mesh, Shape shape)
{
    QList<Vertex> vertices;
    QList<quint16> indices;
    switch (shape) {
    case TorusKnot:
        // A (2, 3) torus knot, the trefoil.
        tube(vertices, indices, 256, 20, 0.28f, [](float t) {
            const float r = 0.62f * (2 + std::cos(3 * t));
            return QVector3D(r * std::cos(2 * t), 0.62f * std::sin(3 * t) * 1.2f,
                             r * std::sin(2 * t));
        });
        break;
    case Torus:
        tube(vertices, indices, 96, 32, 0.45f, [](float t) {
            return QVector3D(1.15f * std::cos(t), 0, 1.15f * std::sin(t));
        });
        break;
    case Sphere:
        sphere(vertices, indices, 1.35f);
        break;
    case Cube:
        cube(vertices, indices, 0.95f);
        break;
    }
    const QList<quint16> edges = edgesOf(indices);

    mesh.vertices = upload(QOpenGLBuffer::VertexBuffer, vertices.constData(),
                           int(vertices.size() * sizeof(Vertex)));
    mesh.triangles = upload(QOpenGLBuffer::IndexBuffer, indices.constData(),
                            int(indices.size() * sizeof(quint16)));
    mesh.edges = upload(QOpenGLBuffer::IndexBuffer, edges.constData(),
                        int(edges.size() * sizeof(quint16)));
    mesh.triangleIndexCount = int(indices.size());
    mesh.edgeIndexCount = int(edges.size());
}

void SceneView::resizeGL(int w, int h)
{
    m_projection.setToIdentity();
    m_projection.perspective(42, float(w) / float(qMax(h, 1)), 0.1f, 100);
}

void SceneView::drawMesh(const Mesh &mesh, const QMatrix4x4 &model,
                         const QMatrix4x4 &viewProjection, const QColor &color, bool lit,
                         bool lines)
{
    if (!mesh.vertices)
        return;
    m_program->setUniformValue("mvp", viewProjection * model);
    m_program->setUniformValue("model", model);
    m_program->setUniformValue("normalMatrix", model.normalMatrix());
    m_program->setUniformValue("color", color);
    m_program->setUniformValue("lit", lit ? 1.0f : 0.0f);

    mesh.vertices->bind();
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, position), 3, sizeof(Vertex));
    m_program->setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, normal), 3, sizeof(Vertex));

    const QOpenGLBuffer *indices = lines ? mesh.edges.get() : mesh.triangles.get();
    const int count = lines ? mesh.edgeIndexCount : mesh.triangleIndexCount;
    if (indices && count > 0) {
        const_cast<QOpenGLBuffer *>(indices)->bind();
        glDrawElements(lines ? GL_LINES : GL_TRIANGLES, count, GL_UNSIGNED_SHORT, nullptr);
        const_cast<QOpenGLBuffer *>(indices)->release();
    }
    mesh.vertices->release();
}

void SceneView::paintGL()
{
    glClearColor(0.055f, 0.063f, 0.09f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_program)
        return;

    if (m_meshDirty) {
        buildMesh(m_model, m_shape);
        m_meshDirty = false;
    }

    // The camera orbits the origin.
    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D eye(m_distance * std::cos(pitch) * std::sin(yaw), m_distance * std::sin(pitch),
                        m_distance * std::cos(pitch) * std::cos(yaw));
    QMatrix4x4 view;
    view.lookAt(eye, QVector3D(0, -0.2f, 0), QVector3D(0, 1, 0));
    const QMatrix4x4 viewProjection = m_projection * view;

    m_program->bind();
    m_program->setUniformValue("eye", eye);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawMesh(m_ground, QMatrix4x4(), viewProjection, QColor(90, 110, 150, 150), false, true);

    QMatrix4x4 model;
    model.rotate(m_spin, 0, 1, 0);
    model.rotate(18, 1, 0, 0.3f);
    if (m_wireframe) {
        drawMesh(m_model, model, viewProjection, m_color.lighter(130), false, true);
    } else {
        drawMesh(m_model, model, viewProjection, m_color, true, false);
    }
    m_program->release();

    ++m_frames;
    const qint64 now = m_clock.elapsed();
    if (now - m_fpsSince >= 1000) {
        emit framesPerSecond(int(m_frames * 1000 / (now - m_fpsSince)));
        m_frames = 0;
        m_fpsSince = now;
    }
}

void SceneView::mousePressEvent(QMouseEvent *event)
{
    m_lastPos = event->position();
}

void SceneView::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QPointF delta = event->position() - m_lastPos;
    m_lastPos = event->position();
    m_yaw -= float(delta.x()) * 0.4f;
    m_pitch = qBound(-80.0f, m_pitch + float(delta.y()) * 0.4f, 80.0f);
    update();
}

void SceneView::mouseDoubleClickEvent(QMouseEvent *)
{
    resetView();
}

void SceneView::wheelEvent(QWheelEvent *event)
{
    const float steps = float(event->angleDelta().y()) / 120.0f;
    m_distance = qBound(2.5f, m_distance * std::pow(0.9f, steps), 14.0f);
    update();
    event->accept();
}

// ------------------------------------------------------------------ page

ScenePage::ScenePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *controls = new QHBoxLayout;
    auto *shape = new QComboBox;
    shape->addItems({ QStringLiteral("Torus knot"), QStringLiteral("Torus"),
                      QStringLiteral("Sphere"), QStringLiteral("Cube") });
    auto *color = new QPushButton(QStringLiteral("Color…"));
    auto *wireframe = new QCheckBox(QStringLiteral("Wireframe"));
    auto *spin = new QCheckBox(QStringLiteral("Spin"));
    spin->setChecked(true);
    auto *speed = new QSlider(Qt::Horizontal);
    speed->setRange(0, 180);
    speed->setValue(40);
    speed->setMaximumWidth(140);
    speed->setToolTip(QStringLiteral("Spin speed, degrees per second"));
    auto *reset = new QPushButton(QStringLiteral("Reset view"));
    controls->addWidget(new QLabel(QStringLiteral("Model")));
    controls->addWidget(shape);
    controls->addWidget(color);
    controls->addWidget(wireframe);
    controls->addWidget(spin);
    controls->addWidget(speed);
    controls->addWidget(reset);
    controls->addStretch(1);
    m_fps = new QLabel;
    controls->addWidget(m_fps);
    layout->addLayout(controls);

    m_stack = new QStackedWidget;
    m_view = new SceneView;
    m_unavailable = new QLabel;
    m_unavailable->setAlignment(Qt::AlignCenter);
    m_unavailable->setWordWrap(true);
    m_stack->addWidget(m_view);
    m_stack->addWidget(m_unavailable);
    layout->addWidget(m_stack, 1);

    m_info = new QLabel(QStringLiteral("Drag to orbit, wheel to zoom, double-click to reset."));
    m_info->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(m_info);

    connect(shape, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_view->setShape(SceneView::Shape(index)); });
    connect(color, &QPushButton::clicked, this, &ScenePage::chooseColor);
    connect(wireframe, &QCheckBox::toggled, m_view, &SceneView::setWireframe);
    connect(spin, &QCheckBox::toggled, m_view, &SceneView::setSpinning);
    connect(speed, &QSlider::valueChanged, m_view, &SceneView::setSpinSpeed);
    connect(reset, &QPushButton::clicked, m_view, &SceneView::resetView);
    connect(m_view, &SceneView::framesPerSecond, this,
            [this](int fps) { m_fps->setText(QStringLiteral("%1 fps").arg(fps)); });
    connect(m_view, &SceneView::initialized, this, [this](bool ok) {
        if (!ok) {
            showUnavailable();
            return;
        }
        m_info->setText(QStringLiteral("Drag to orbit, wheel to zoom, double-click to reset.  ·  %1")
                                .arg(m_view->rendererInfo()));
        emit message(m_view->rendererInfo());
    });

}

void ScenePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // QOpenGLWidget creates its context when it is first shown. When there
    // is no context to be had, nothing is called back, so look once.
    QTimer::singleShot(400, this, [this] {
        if (m_view->isVisible() && !m_view->isValid())
            showUnavailable();
    });
}

void ScenePage::chooseColor()
{
    const QColor color = QColorDialog::getColor(m_view->color(), this,
                                                QStringLiteral("Model color"));
    if (color.isValid())
        m_view->setColor(color);
}

void ScenePage::showUnavailable()
{
    m_unavailable->setText(QStringLiteral(
            "<p><b>OpenGL is not available here.</b></p>"
            "<p>This page draws with a QOpenGLWidget. Hosted in a tab, that needs the "
            "app to run in GPU mode (QmlBrowserHost::Graphics::Gpu); in Software mode it "
            "runs on Qt's offscreen platform, which has no OpenGL on macOS and Windows.</p>"));
    m_stack->setCurrentWidget(m_unavailable);
}
