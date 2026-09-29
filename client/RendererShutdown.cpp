#include "RendererShutdown.h"

#include "IpcChannel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace RendererShutdown {

namespace {

// How long every renderer, together, gets to notice that its channel closed.
constexpr int kGraceMs = 200;

// One budget for every process that had to be killed. QProcess::kill() is
// SIGKILL on Unix and TerminateProcess on Windows; either should already have
// ended the process, and this only waits for the operating system to reap it.
constexpr int kExitGraceMs = 2000;

enum class Phase { Idle, ShuttingDown, Done };

struct Entry {
    QPointer<QProcess> process;
    QPointer<IpcChannel> channel;
    // The owning tab has already dropped its pointer. shutdown() deletes these
    // once they are reaped. A process a tab still points at is left for that
    // tab's destructor, which only runs ~QProcess after the child is gone.
    bool released = false;
};

QList<Entry> g_entries;
Phase g_phase = Phase::Idle;
bool g_hooked = false;

Entry *findEntry(QProcess *process)
{
    for (Entry &entry : g_entries) {
        if (entry.process == process)
            return &entry;
    }
    return nullptr;
}

void removeEntry(QProcess *process)
{
    for (int i = g_entries.size() - 1; i >= 0; --i) {
        if (g_entries.at(i).process == process || g_entries.at(i).process.isNull())
            g_entries.removeAt(i);
    }
}

bool outsideEventLoop()
{
    return QThread::currentThread()->loopLevel() == 0
            || !QCoreApplication::instance()
            || QCoreApplication::closingDown();
}

// waitForFinished() blocks on one process, but its nested loop still delivers
// the other processes' death notifications. Slicing the budget keeps the total
// wait on the clock instead of on the number of processes.
void waitForAll(int budgetMs)
{
    QElapsedTimer timer;
    timer.start();
    while (true) {
        const int elapsed = int(timer.elapsed());
        if (elapsed >= budgetMs)
            return;

        // Index walk: waitForFinished() can retire another tab, and that
        // appends to g_entries. A range-for iterator would not survive it.
        QProcess *pending = nullptr;
        for (int i = 0; i < g_entries.size(); ++i) {
            QProcess *process = g_entries.at(i).process;
            if (process && process->state() != QProcess::NotRunning) {
                pending = process;
                break;
            }
        }
        if (!pending)
            return;

        pending->waitForFinished(std::min(budgetMs - elapsed, 50));
    }
}

// ~QProcess waits forever, and warns, if the child is still alive. A process
// that survived kill() is detached instead of destroyed.
void discardIfStillRunning(QProcess *process)
{
    if (!process || process->state() == QProcess::NotRunning)
        return;
    process->setParent(nullptr);
    qWarning("QmlBrowser: renderer pid %lld did not exit during shutdown",
             static_cast<long long>(process->processId()));
}

// finished() is wired to deleteLater while a tab is retired in the background.
// waitForFinished() runs a nested loop, so a delete posted earlier would free
// the QProcess on that stack. Drop those events; shutdown() deletes the object
// itself once the child is gone.
void disarmDeleteLater(QProcess *process)
{
    process->disconnect();
    if (QCoreApplication *app = QCoreApplication::instance())
        app->removePostedEvents(process, QEvent::DeferredDelete);
}

} // namespace

void watch(QProcess *process)
{
    if (!process || findEntry(process))
        return;

    if (!g_hooked) {
        if (QCoreApplication *app = QCoreApplication::instance()) {
            g_hooked = true;
            QObject::connect(app, &QCoreApplication::aboutToQuit, app, [] { shutdown(); });
        }
    }

    Entry entry;
    entry.process = process;
    g_entries.append(entry);
}

void bindChannel(QProcess *process, IpcChannel *channel)
{
    if (Entry *entry = findEntry(process))
        entry->channel = channel;
}

void shutdown()
{
    if (g_phase != Phase::Idle)
        return;
    g_phase = Phase::ShuttingDown;

    QList<QPointer<QProcess>> processes;
    QList<IpcChannel *> channels;
    for (const Entry &entry : g_entries) {
        if (QProcess *process = entry.process)
            processes.append(process);
        if (IpcChannel *channel = entry.channel)
            channels.append(channel);
    }

    // Drop finished → deleteLater before doing anything that can pump events.
    QCoreApplication *app = QCoreApplication::instance();
    for (const QPointer<QProcess> &process : processes) {
        if (!process)
            continue;
        disarmDeleteLater(process);
        if (app)
            process->setParent(app);
    }

    // Closing the channel is what asks a renderer to quit. QProcess::terminate()
    // is the wrong signal: on Windows it posts WM_CLOSE to top-level windows,
    // and this renderer's window is a child of the browser.
    for (IpcChannel *channel : channels)
        channel->close();

    waitForAll(kGraceMs);

    for (const QPointer<QProcess> &process : processes) {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    }

    waitForAll(kExitGraceMs);

    for (int i = 0; i < g_entries.size(); ++i) {
        QProcess *process = g_entries.at(i).process;
        if (!process)
            continue;
        if (process->state() != QProcess::NotRunning) {
            discardIfStillRunning(process);
            continue;
        }
        // Tabs that still hold the pointer delete it from retire(), once
        // shutdown has already reaped the child.
        if (g_entries.at(i).released)
            delete process;
    }

    g_entries.clear();
    g_phase = Phase::Done;
}

void retire(QProcess *process)
{
    if (!process)
        return;

    if (g_phase == Phase::Done) {
        // shutdown() already spent the shared grace and the shared kill budget.
        // Waiting again here would put the tab count back into the exit time.
        disarmDeleteLater(process);
        if (process->state() == QProcess::NotRunning)
            delete process;
        else
            discardIfStillRunning(process);
        return;
    }

    if (Entry *entry = findEntry(process))
        entry->released = true;
    else {
        Entry created;
        created.process = process;
        created.released = true;
        g_entries.append(created);
    }

    disarmDeleteLater(process);
    if (QCoreApplication *app = QCoreApplication::instance())
        process->setParent(app);

    // Already inside the shared wait. It deletes released processes at the end.
    if (g_phase == Phase::ShuttingDown)
        return;

    // The first tab destroyed after the event loop has stopped reaps every
    // renderer here, including siblings whose destructors have not run yet.
    if (outsideEventLoop()) {
        shutdown();
        return;
    }

    if (process->state() == QProcess::NotRunning) {
        removeEntry(process);
        delete process;
        return;
    }

    QObject::connect(process, &QProcess::finished, process, [process] {
        if (g_phase != Phase::Idle)
            return;
        removeEntry(process);
        process->deleteLater();
    });
    QTimer::singleShot(kExitGraceMs, process, [process] {
        if (g_phase == Phase::Idle && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

} // namespace RendererShutdown
