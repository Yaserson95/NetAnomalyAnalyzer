#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

/**
 * @brief Запись журнала обращения к LLM.
 */
class LlmJournalEntry {
public:
    enum class Status { Pending, Success, Error };
    enum class Source { Scheduled, Manual, Test };

    LlmJournalEntry() = default;

    static QString statusName(Status status);
    static QString sourceName(Source source);
    /** Человекочитаемый вердикт: ok → «Норма», warning → «Внимание», … */
    static QString verdictName(const QString &verdictStatus);

    qint64 id = 0;
    Source  source = Source::Manual;
    Status  status = Status::Pending;
    QDateTime startTime;
    QDateTime finishTime;
    QString model;
    QString prompt;            ///< полный промпт
    QString promptPreview;     ///< начало промпта (для компактных колонок)
    QString response;          ///< полный ответ модели
    QString error;
    qint64  elapsedMs = 0;
    int     anomaliesInPrompt = 0;
    QString logPeriodFrom;     ///< начало периода журнала событий (ISO)
    QString verdictStatus;     ///< ok/warning/critical (пусто — нет/не распознан)
    QJsonObject verdictData;   ///< data из ответа LLM

    qint64 durationSec() const;
    QString shortResponse(int maxLen = 200) const;
    QJsonObject toJson() const;
    /** Восстановить запись из JSON-записи llm-journal.jsonl. */
    static LlmJournalEntry fromJson(const QJsonObject &object);
};
