#include "LlmJournal.h"
#include "../logging/LogWriter.h"
#include "../settings/Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

LlmJournal::LlmJournal(QObject *parent)
    : QObject(parent)
{
}

const LlmJournalEntry *LlmJournal::entry(qint64 id) const
{
    for (const LlmJournalEntry &item : m_entries) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

LlmJournalEntry *LlmJournal::find(qint64 id)
{
    for (LlmJournalEntry &item : m_entries) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

qint64 LlmJournal::beginEntry(LlmJournalEntry::Source source, const QString &model,
                              const QString &prompt, int anomaliesInPrompt,
                              const QString &logPeriodFrom)
{
    LlmJournalEntry record;
    record.id = m_nextId++;
    record.source = source;
    record.status = LlmJournalEntry::Status::Pending;
    record.startTime = QDateTime::currentDateTime();
    record.model = model;
    record.prompt = prompt;
    record.promptPreview = prompt.left(1000);
    record.anomaliesInPrompt = anomaliesInPrompt;
    record.logPeriodFrom = logPeriodFrom;

    m_entries.append(record);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeFirst();

    emit entryAdded(record.id);
    return record.id;
}

void LlmJournal::finishSuccess(qint64 id, const QString &response, qint64 elapsedMs,
                               const QString &verdictStatus,
                               const QJsonObject &verdictData)
{
    LlmJournalEntry *record = find(id);
    if (!record)
        return;
    record->status = LlmJournalEntry::Status::Success;
    record->finishTime = QDateTime::currentDateTime();
    record->response = response;
    record->elapsedMs = elapsedMs;
    record->verdictStatus = verdictStatus;
    record->verdictData = verdictData;
    appendToFile(*record);
    emit entryUpdated(id);
}

void LlmJournal::finishError(qint64 id, const QString &error, qint64 elapsedMs)
{
    LlmJournalEntry *record = find(id);
    if (!record)
        return;
    record->status = LlmJournalEntry::Status::Error;
    record->finishTime = QDateTime::currentDateTime();
    record->error = error;
    record->elapsedMs = elapsedMs;
    appendToFile(*record);
    emit entryUpdated(id);
}

void LlmJournal::appendToFile(const LlmJournalEntry &entry)
{
    const QString dir = Settings::instance().logging().logDir;
    QDir().mkpath(dir);
    QFile file(dir + QLatin1String("/llm-journal.jsonl"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(QJsonDocument(entry.toJson()).toJson(QJsonDocument::Compact) + '\n');
}

void LlmJournal::clear()
{
    m_entries.clear();
    emit modelReset();
}

void LlmJournal::loadFrom(const QVector<LlmJournalEntry> &entries)
{
    m_entries = entries;
    for (const LlmJournalEntry &item : m_entries)
        m_nextId = qMax(m_nextId, item.id + 1);
    emit modelReset();
}

QVector<LlmJournalEntry> LlmJournal::loadFromFile(const QString &logDir, int maxEntries)
{
    QVector<LlmJournalEntry> result;
    const QVector<QJsonObject> records =
        readJsonlTail(logDir + QLatin1String("/llm-journal.jsonl"), maxEntries);
    result.reserve(records.size());
    for (const QJsonObject &record : records)
        result.append(LlmJournalEntry::fromJson(record));
    return result;
}
