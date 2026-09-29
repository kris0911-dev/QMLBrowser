#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

class IpcChannel;

// Tracks live and retiring renderer processes so the browser can stop all of
// them in one pass when it quits.
//
// Closing each process on its own, then waiting out a grace period and a kill
// timeout, freezes quit for roughly (grace + kill wait) times the number of
// stuck renderers. shutdown() closes every channel first, waits once, kills
// whoever is left, and waits once more. The whole wait is a fixed budget.
namespace RendererShutdown {

// Registers a process that startProcess() has just created. The process stays
// parented to its tab until it is retired or the browser shuts down.
void watch(QProcess *process);

// The channel whose close is the renderer's cue to exit. May be bound after
// watch(), once the renderer has connected.
void bindChannel(QProcess *process, IpcChannel *channel);

// The tab has dropped this renderer. While the browser is still running the
// process is reaped in the background. During teardown this joins the single
// shared shutdown instead of waiting on its own.
void retire(QProcess *process);

// Idempotent. Safe from aboutToQuit and from ~TabManager.
void shutdown();

} // namespace RendererShutdown
