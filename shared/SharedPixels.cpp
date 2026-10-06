#include "SharedPixels.h"

bool SharedPixels::create(const QString &key, int bytes)
{
    if (bytes < 1)
        return false;

    detach();
    m_memory.setKey(key);
    if (m_memory.create(bytes))
        return true;

    // A previous process can leave the name behind. Attach and drop it, then
    // the same key can be created again.
    if (m_memory.error() == QSharedMemory::AlreadyExists) {
        m_memory.attach();
        m_memory.detach();
        if (m_memory.create(bytes))
            return true;
    }
    return false;
}

bool SharedPixels::attach(const QString &key)
{
    if (m_memory.isAttached() && m_memory.key() == key)
        return true;

    detach();
    m_memory.setKey(key);
    return m_memory.attach();
}

void SharedPixels::detach()
{
    if (m_memory.isAttached())
        m_memory.detach();
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
    return m_memory.key();
}

bool SharedPixels::isAttached() const
{
    return m_memory.isAttached();
}

QString SharedPixels::errorString() const
{
    return m_memory.errorString();
}
