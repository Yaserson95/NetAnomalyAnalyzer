#pragma once

#include "LlmClient.h"
#include "LlmJournal.h"
#include "LlmSentStore.h"
#include "LlmVerdict.h"
#include "../anomaly/Anomaly.h"

#include <QObject>
#include <QTimer>

class LogWriter;
class StatisticsCollector;

/**
 * @brief Планировщик LLM-анализа.
 *
 * Собирает контекст (статистика сети + новые записи журнала аномалий со
 * статусами + журнал событий с момента предыдущего запроса),
 * отправляет его в LLM либо по таймеру (раз в настроенный интервал),
 * либо по запросу из интерфейса. Каждый вызов фиксируется в журнале.
 *
 * Аномалии, уже отправленные в LLM (успешно), повторно в контекст не
 * попадают — учёт ведётся в LlmSentStore (<логи>/llm-sent.json).
 *
 * Модель обязана отвечать строго JSON-объектом
 * {"status":"ok|warning|critical","data":{...}} — разобранный вердикт
 * сохраняется в журнале, а при warning/critical порождает оповещение
 * (сигнал verdictAnomaly).
 */
class LlmScheduler : public QObject {
    Q_OBJECT
public:
    LlmScheduler(LlmClient *client, StatisticsCollector *statistics,
                 LogWriter *logWriter, QObject *parent = nullptr);

    LlmJournal *journal() { return &m_journal; }
    LlmClient *client() { return m_client; }

    bool isBusy() const;

public slots:
    /** Запустить/остановить таймер согласно настройкам. */
    void applySettings();
    /** Анализ по запросу пользователя. */
    void requestManualAnalysis();
    /** Отправить простой тест-запрос. */
    void sendTestRequest();

signals:
    void statusChanged(const QString &status);
    void analysisFinished(bool success, const QString &summary);
    /** Вердикт LLM уровня warning/critical — для оповещений. */
    void verdictAnomaly(const Anomaly &anomaly);

private slots:
    void runScheduled();

private:
    void startAnalysis(LlmJournalEntry::Source source);
    /** Отправить один чанк записей; при contextChunks>1 шлётся преамбула. */
    void sendChunk(const QString &prompt, int anomaliesInPrompt,
                   const QString &logPeriodFromIso, LlmJournalEntry::Source source);
    /** Отправить следующий чанк конвейера либо завершить анализ агрегатом. */
    void advanceChunkPipeline();
    /** Хвост журнала аномалий, из которого исключены уже отправленные в LLM.
     *  В skippedAlreadySent — сколько записей пропущено как отправленные. */
    QVector<QJsonObject> newAnomaliesForPrompt(int *skippedAlreadySent) const;
    QString buildPrompt(const QVector<QJsonObject> &anomalies,
                        int skippedAlreadySent, qint64 eventsSinceMs,
                        const QString &logPeriodFromIso) const;
    /** Ключи записей («id@timestamp») для отметки «отправлено». */
    static QStringList anomalyKeys(const QVector<QJsonObject> &anomalies);
    /** Момент последнего завершённого анализа (не тестового), мс; 0 — нет. */
    qint64 lastAnalysisTime() const;
    static Anomaly verdictToAnomaly(const LlmVerdict &verdict);

    LlmClient            *m_client;
    StatisticsCollector   *m_statistics;
    LogWriter             *m_logWriter;
    LlmJournal             m_journal;
    LlmSentStore           m_sentStore;   ///< уже отправленные в LLM аномалии
    QTimer                 m_timer;
    qint64                 m_currentEntryId = 0;
    LlmJournalEntry::Source m_currentSource = LlmJournalEntry::Source::Manual;
    qint64                 m_currentStartedAt = 0;

    // Конвейер отправки чанков одного анализа (запрос может делиться,
    // если контекст превышает бюджет токенов).
    QVector<QString>          m_chunkPrompts;
    QVector<int>              m_chunkAnomalies;
    QVector<QString>          m_chunkPeriodFrom;
    QVector<QStringList>      m_chunkKeys;      ///< аномалии каждого чанка
    QStringList               m_inflightKeys;   ///< аномалии запроса в полёте
    int                       m_chunkIndex = 0;
    LlmVerdict::Level         m_aggregateLevel = LlmVerdict::Level::Unknown;
    QStringList               m_aggregateSummaries;
    QVector<LlmVerdict::Finding> m_aggregateFindings;
    qint64                    m_aggregateElapsed = 0;
    int                       m_chunksCompleted = 0;
    int                       m_chunksFailed = 0;
    bool                      m_chunkRunActive = false;
};
