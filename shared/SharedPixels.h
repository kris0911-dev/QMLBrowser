#pragma once

#include <QSharedMemory>
#include <QString>

// One shared-memory bitmap, passed from a renderer process to the browser.
//
// This is the software path of a compositor frame: the renderer writes pixels,
// the browser reads them, and a lock on each side keeps a write from tearing
// a read. The browser window is what actually shows the pixels, so the frame
// has no window of its own.
class SharedPixels
{
public:
    bool create(const QString &key, int bytes);
    bool attach(const QString &key);
    void detach();

    bool lock();
    void unlock();

    uchar *data();
    const uchar *data() const;
    int size() const;
    QString key() const;
    bool isAttached() const;
    QString errorString() const;

private:
    void assignKey(const QString &key);

    // The name both processes agree on. QSharedMemory::key() is empty when the
    // segment is opened with a native POSIX key, so this is stored separately.
    QString m_key;
    QSharedMemory m_memory;
};
