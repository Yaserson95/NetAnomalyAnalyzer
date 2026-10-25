#pragma once

#include "LlmJournalEntry.h"

#include <QObject>
#include <QVector>

/**
 * @brief Хранилище записей журнала обращений к LLM.
 *
 * Хранит записи в памяти (для модели таблицы) и дописывает каждую
 * завершённую запись в файл llm-journal.jsonl через LogWriter-хук.
 */
class LlmJournal : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxEntries = 1000;

    explicit LlmJournal(QObject *parent = nullptr);

    const QVector<LlmJournalEntry> &entries() const { return m_entries; }
    const LlmJournalEntry *entry(qint64 id) const;

    /** Создать запись со статусом Pending и вернуть её идентификатор. */
    qint64 beginEntry(LlmJournalEntry::Source source, const QString &model,
                      const QString &prompt, int anomaliesInPrompt,
                      const QString &logPeriodFrom = QString());
    void finishSuccess(qint64 id, const QString &response, qint64 elapsedMs,
                       const QString &verdictStatus = QString(),
                       const QJsonObject &verdictData = QJsonObject());
    void finishError(qint64 id, const QString &error, qint64 elapsedMs);

    void clear();
    void loadFrom(const QVector<LlmJournalEntry> &entries);
    /** Загрузить журнал из файла llm-journal.jsonl (для истории при старте). */
    static QVector<LlmJournalEntry> loadFromFile(const QString &logDir,
                                                 int maxEntries = kMaxEntries);

signals:
    void entryAdded(qint64 id);
    void entryUpdated(qint64 id);
    void modelReset();

private:
    LlmJournalEntry *find(qint64 id);
    void appendToFile(const LlmJournalEntry &entry);

    QVector<LlmJournalEntry> m_entries;
    qint64 m_nextId = 1;
};
