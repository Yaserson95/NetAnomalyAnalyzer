#include "RateWindow.h"

#include <algorithm>

RateWindow::RateWindow(qint64 windowMs)
    : m_windowMs(windowMs)
{
}

void RateWindow::add(const QString &key, qint64 nowMs)
{
    Entry &entry = m_entries[key];
    entry.stamps.append(nowMs);
    // Отсекаем отметки вне окна, чтобы память не росла бесконечно.
    auto it = std::partition(entry.stamps.begin(), entry.stamps.end(),
                             [this, nowMs](qint64 ts) { return nowMs - ts <= m_windowMs; });
    entry.stamps.erase(it, entry.stamps.end());
}

int RateWindow::count(const QString &key, qint64 nowMs) const
{
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return 0;

    Entry &entry = it.value();
    auto end = std::partition(entry.stamps.begin(), entry.stamps.end(),
                              [this, nowMs](qint64 ts) { return nowMs - ts <= m_windowMs; });
    entry.stamps.erase(end, entry.stamps.end());
    if (entry.stamps.isEmpty())
        m_entries.remove(key);
    return entry.stamps.size();
}

void RateWindow::remove(const QString &key)
{
    m_entries.remove(key);
}

void RateWindow::clear()
{
    m_entries.clear();
}
