#pragma once

#include <QHash>
#include <QString>

/**
 * @brief Скользящее окно счётчика событий (для оценки скорости).
 *
 * Хранит отметки времени для каждого ключа и умеет подсчитывать
 * события за последние windowMs миллисекунд.
 */
class RateWindow {
public:
    explicit RateWindow(qint64 windowMs = 1000);

    void setWindowMs(qint64 windowMs) { m_windowMs = windowMs; }
    qint64 windowMs() const { return m_windowMs; }

    /** Зарегистрировать событие по ключу. */
    void add(const QString &key, qint64 nowMs);
    /** Число событий по ключу за окно (устаревшие отметки удаляются). */
    int count(const QString &key, qint64 nowMs) const;
    /** Удалить все отметки по ключу. */
    void remove(const QString &key);
    /** Полный сброс. */
    void clear();

private:
    struct Entry {
        QList<qint64> stamps;
    };
    mutable QHash<QString, Entry> m_entries;
    qint64 m_windowMs = 1000;
};
