#include "PageTextCache.h"

PageTextCache::PageTextCache(QObject *parent)
    : QObject(parent)
{
}

std::optional<QString> PageTextCache::get(const QString &filePath, int page)
{
    QReadLocker lock(&m_lock);
    auto it = m_cache.find({filePath, page});
    if (it == m_cache.end())
        return std::nullopt;
    return it->text;
}

void PageTextCache::put(const QString &filePath, int page, const QString &text)
{
    QWriteLocker lock(&m_lock);
    PageTextCacheKey key{filePath, page};
    m_cache[key] = {text, QDateTime::currentDateTime()};
    evictIfNeeded();
}

void PageTextCache::invalidate(const QString &filePath)
{
    QWriteLocker lock(&m_lock);
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (it.key().filePath == filePath)
            it = m_cache.erase(it);
        else
            ++it;
    }
}

void PageTextCache::clear()
{
    QWriteLocker lock(&m_lock);
    m_cache.clear();
}

void PageTextCache::setMaxEntries(int n)
{
    QWriteLocker lock(&m_lock);
    m_maxEntries = qMax(1, n);
    evictIfNeeded();
}

void PageTextCache::evictIfNeeded()
{
    while (m_cache.size() > m_maxEntries) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it->accessTime < oldest->accessTime)
                oldest = it;
        }
        m_cache.erase(oldest);
    }
}
