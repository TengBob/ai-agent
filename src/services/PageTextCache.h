#pragma once
#include <QObject>
#include <QHash>
#include <QReadWriteLock>
#include <QString>
#include <QDateTime>
#include <optional>

struct PageTextCacheKey
{
    QString filePath;
    int page = 0;

    bool operator==(const PageTextCacheKey &other) const noexcept
    {
        return page == other.page && filePath == other.filePath;
    }
};

inline size_t qHash(const PageTextCacheKey &key, size_t seed = 0) noexcept
{
    return qHash(key.filePath, seed) ^ static_cast<size_t>(key.page);
}

class PageTextCache : public QObject
{
    Q_OBJECT
public:
    explicit PageTextCache(QObject *parent = nullptr);

    std::optional<QString> get(const QString &filePath, int page);
    void put(const QString &filePath, int page, const QString &text);
    void invalidate(const QString &filePath);
    void clear();
    void setMaxEntries(int n);

private:
    struct Entry {
        QString text;
        QDateTime accessTime;
    };

    void evictIfNeeded();

    QReadWriteLock m_lock;
    QHash<PageTextCacheKey, Entry> m_cache;
    int m_maxEntries = 10;
};
