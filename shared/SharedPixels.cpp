#include "SharedPixels.h"

void SharedPixels::assignKey(const QString &key)
{
#if defined(Q_OS_MACOS)
    // setKey() selects System V shared memory. macOS ships with
    // kern.sysv.shmmax at 4 MiB, and one retina frame is larger than that, so
    // create() fails with "out of resources". POSIX shared memory is sized to
    // the request. Both sides must use the same native key for a given name.
    if (QSharedMemory::isKeyTypeSupported(QNativeIpcKey::Type::PosixRealtime)) {
        m_memory.setNativeKey(
                QSharedMemory::platformSafeKey(key, QNativeIpcKey::Type::PosixRealtime));
        return;
    }
#endif
    m_memory.setKey(key);
}

bool SharedPixels::create(const QString &key, int bytes)
{
    if (bytes < 1)
        return false;

    detach();
    assignKey(key);
    if (m_memory.create(bytes)) {
        m_key = key;
        return true;
    }

    // A previous process can leave the name behind. Attach and drop it, then
    // the same key can be created again.
    if (m_memory.error() == QSharedMemory::AlreadyExists) {
        m_memory.attach();
        m_memory.detach();
        assignKey(key);
        if (m_memory.create(bytes)) {
            m_key = key;
            return true;
        }
    }
    m_key.clear();
    return false;
}

bool SharedPixels::attach(const QString &key)
{
    if (m_memory.isAttached() && m_key == key)
        return true;

    detach();
    assignKey(key);
    if (!m_memory.attach())
        return false;
    m_key = key;
    return true;
}

void SharedPixels::detach()
{
    if (m_memory.isAttached())
        m_memory.detach();
    m_key.clear();
}

bool SharedPixels::lock()
{
    return m_memory.lock();
}

void SharedPixels::unlock()
{
    m_memory.unlock();
}

uchar *SharedPixels::data()
{
    return static_cast<uchar *>(m_memory.data());
}

const uchar *SharedPixels::data() const
{
    return static_cast<const uchar *>(m_memory.data());
}

int SharedPixels::size() const
{
    return m_memory.isAttached() ? m_memory.size() : 0;
}

QString SharedPixels::key() const
{
    return m_key;
}

bool SharedPixels::isAttached() const
{
    return m_memory.isAttached();
}

QString SharedPixels::errorString() const
{
    return m_memory.errorString();
}
