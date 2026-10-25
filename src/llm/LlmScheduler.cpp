#include "LlmScheduler.h"
#include "../anomaly/AnomalyStatusStore.h"
#include "../logging/LogWriter.h"
#include "../settings/Settings.h"
#include "../stats/StatisticsCollector.h"
#include "PromptBudget.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include <utility>

LlmScheduler::LlmScheduler(LlmClient *client, StatisticsCollector *statistics,
                           LogWriter *logWriter, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_statistics(statistics)
    , m_logWriter(logWriter)
{
    // История журнала обращений — восстанавливаем из файла при старте,
    // чтобы записи (и момент последнего запроса) не терялись между запусками.
    m_journal.loadFrom(
        LlmJournal::loadFromFile(Settings::instance().logging().logDir));

    // Учёт уже отправленных аномалий: в контекст попадают только новые.
    m_sentStore.setFilePath(
        Settings::instance().logging().logDir
        + QStringLiteral("/llm-sent.json"));

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &LlmScheduler::runScheduled);

    connect(m_client, &LlmClient::responseReceived, this,
            [this](const QString &response, qint64 elapsedMs) {
                if (!m_chunkRunActive || m_currentEntryId <= 0) {
                    // одиночный запрос (обычный анализ или тест)
                    LlmVerdict verdict;
                    if (m_currentSource != LlmJournalEntry::Source::Test)
                        verdict = LlmVerdict::fromResponse(response);
                    m_journal.finishSuccess(m_currentEntryId, response, elapsedMs,
                                            verdict.statusText, verdict.data);
                    // Запрос дошёл до модели — отправленные аномалии больше
                    // не повторяем (при ошибке они уйдут в следующий запрос).
                    m_sentStore.markSent(m_inflightKeys);
                    m_inflightKeys.clear();
                    if (verdict.parsed
                        && verdict.level != LlmVerdict::Level::Ok
                        && verdict.level != LlmVerdict::Level::Unknown)
                        emit verdictAnomaly(verdictToAnomaly(verdict));
                    emit statusChanged(
                        tr("LLM: ответ получен (%1 мс)%2")
                            .arg(elapsedMs)
                            .arg(verdict.parsed
                                     ? tr(", вердикт: %1")
                                           .arg(LlmVerdict::levelName(verdict.level))
                                     : QString()));
                    emit analysisFinished(true, response.left(300));
                    return;
                }

                // чанк конвейера
                LlmVerdict verdict;
                if (m_currentSource != LlmJournalEntry::Source::Test)
                    verdict = LlmVerdict::fromResponse(response);
                m_journal.finishSuccess(m_currentEntryId, response, elapsedMs,
                                        verdict.statusText, verdict.data);
                if (verdict.parsed) {
                    if (verdict.level == LlmVerdict::Level::Critical)
                        m_aggregateLevel = LlmVerdict::Level::Critical;
                    else if (verdict.level == LlmVerdict::Level::Warning
                             && m_aggregateLevel != LlmVerdict::Level::Critical)
                        m_aggregateLevel = LlmVerdict::Level::Warning;
                    else if (m_aggregateLevel == LlmVerdict::Level::Unknown)
                        m_aggregateLevel = verdict.level;
                    if (!verdict.summary.isEmpty())
                        m_aggregateSummaries.append(verdict.summary);
                    m_aggregateFindings += verdict.findings;
                }
                m_aggregateElapsed += elapsedMs;
                ++m_chunksCompleted;
                m_sentStore.markSent(m_inflightKeys);   // чанк дошёл до модели
                m_inflightKeys.clear();
                advanceChunkPipeline();
            });

    connect(m_client, &LlmClient::requestFailed, this, [this](const QString &error) {
        if (m_logWriter)
            m_logWriter->logError(QStringLiteral("llm"), error);
        if (!m_chunkRunActive || m_currentEntryId <= 0) {
            m_journal.finishError(m_currentEntryId, error,
                                  m_currentStartedAt ? QDateTime::currentMSecsSinceEpoch() - m_currentStartedAt : 0);
            m_inflightKeys.clear();   // не отмечаем — уйдёт повторно
            emit statusChanged(tr("LLM: ошибка — %1").arg(error));
            emit analysisFinished(false, error);
            return;
        }
        // ошибка чанка: фиксируем в журнале, но продолжаем конвейер
        m_journal.finishError(m_currentEntryId, error,
                              m_currentStartedAt ? QDateTime::currentMSecsSinceEpoch() - m_currentStartedAt : 0);
        m_inflightKeys.clear();       // аномалии чанка уйдут повторно
        ++m_chunksFailed;
        emit statusChanged(tr("LLM: ошибка чанка — %1").arg(error));
        advanceChunkPipeline();
    });

    connect(&Settings::instance(), &Settings::llmChanged, this, &LlmScheduler::applySettings);
    applySettings();
}

bool LlmScheduler::isBusy() const
{
    return m_client && m_client->isBusy();
}

void LlmScheduler::applySettings()
{
    m_timer.stop();
    const LlmSettings &llm = Settings::instance().llm();
    if (!llm.enabled || llm.intervalMin <= 0)
        return;
    m_timer.start(llm.intervalMin * 60 * 1000);
    emit statusChanged(tr("LLM: анализ по расписанию каждые %1 мин.").arg(llm.intervalMin));
}

void LlmScheduler::runScheduled()
{
    const LlmSettings &llm = Settings::instance().llm();
    if (llm.enabled)
        startAnalysis(LlmJournalEntry::Source::Scheduled);

    if (llm.enabled && llm.intervalMin > 0)
        m_timer.start(llm.intervalMin * 60 * 1000);
}

void LlmScheduler::requestManualAnalysis()
{
    startAnalysis(LlmJournalEntry::Source::Manual);
}

void LlmScheduler::sendTestRequest()
{
    if (!m_client || isBusy())
        return;
    const LlmSettings llm = Settings::instance().llm();
    m_currentSource = LlmJournalEntry::Source::Test;
    m_currentEntryId = m_journal.beginEntry(m_currentSource, llm.model,
                                            QStringLiteral("Тест-запрос"), 0);
    m_currentStartedAt = QDateTime::currentMSecsSinceEpoch();
    emit statusChanged(tr("LLM: выполняется тест-запрос…"));
    m_client->testConnection(llm);
}

QVector<QJsonObject> LlmScheduler::newAnomaliesForPrompt(int *skippedAlreadySent) const
{
    const LlmSettings &llm = Settings::instance().llm();
    const QVector<QJsonObject> tail = m_logWriter
            ? m_logWriter->tailAnomalies(llm.contextLines)
            : QVector<QJsonObject>();

    QVector<QJsonObject> fresh;
    fresh.reserve(tail.size());
    int skipped = 0;
    for (const QJsonObject &item : tail) {
        if (m_sentStore.contains(AnomalyStatusStore::keyFor(item)))
            ++skipped;
        else
            fresh.append(item);
    }
    if (skippedAlreadySent)
        *skippedAlreadySent = skipped;
    return fresh;
}

QStringList LlmScheduler::anomalyKeys(const QVector<QJsonObject> &anomalies)
{
    QStringList keys;
    keys.reserve(anomalies.size());
    for (const QJsonObject &item : anomalies)
        keys.append(AnomalyStatusStore::keyFor(item));
    return keys;
}

QString LlmScheduler::buildPrompt(const QVector<QJsonObject> &anomalies,
                                  int skippedAlreadySent, qint64 eventsSinceMs,
                                  const QString &logPeriodFromIso) const
{
    QString prompt;
    prompt += QStringLiteral("=== СВОДКА ПО СЕТИ ===\n");
    prompt += m_statistics ? m_statistics->summaryText() : QStringLiteral("нет данных");

    prompt += QStringLiteral("\n=== ОБНАРУЖЕННЫЕ АНОМАЛИИ (новые, %1) ===\n")
                  .arg(anomalies.size());
    if (anomalies.isEmpty()) {
        if (skippedAlreadySent > 0) {
            prompt += QStringLiteral(
                          "Новых аномалий нет: %1 последних записей журнала уже "
                          "отправлены в анализ и повторно не передаются.\n")
                          .arg(skippedAlreadySent);
        } else {
            prompt += QStringLiteral("Аномалии не обнаруживались.\n");
        }
    } else {
        QJsonArray array;
        for (const QJsonObject &item : std::as_const(anomalies))
            array.append(item);
        prompt += QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented));
        prompt += QLatin1Char('\n');
        if (skippedAlreadySent > 0) {
            prompt += QStringLiteral(
                          "Ещё %1 аномалий журнала уже отправлены в анализ ранее "
                          "и из контекста исключены.\n").arg(skippedAlreadySent);
        }
        prompt += QStringLiteral("Поле \"status\": \"resolved\" — аномалия уже отмечена "
                                 "оператором решённой, повторно её анализировать не нужно.\n");
    }

    // Журнал событий подсистем за период с предыдущего запроса к LLM.
    prompt += QStringLiteral("\n=== ЖУРНАЛ СОБЫТИЙ ЗА ПЕРИОД (с %1 по %2) ===\n")
                  .arg(logPeriodFromIso,
                       QDateTime::currentDateTime().toString(Qt::ISODate));
    if (m_logWriter) {
        const QVector<QJsonObject> events =
            m_logWriter->eventsSince(eventsSinceMs, 200);
        if (events.isEmpty()) {
            prompt += QStringLiteral("Событий за период не было.\n");
        } else {
            for (const QJsonObject &item : std::as_const(events)) {
                prompt += QString::fromUtf8(
                    QJsonDocument(item).toJson(QJsonDocument::Compact));
                prompt += QLatin1Char('\n');
            }
        }
    }

    prompt += QStringLiteral("\nЗадача: оцени состояние сети, выдели наиболее опасные "
                             "события и предложи действия.\n");

    prompt += QStringLiteral(
        "\n=== ФОРМАТ ОТВЕТА ===\n"
        "Ответь СТРОГО одним JSON-объектом вида "
        "{\"status\":\"ok|warning|critical\",\"data\":{...}} "
        "без какого-либо текста вне JSON и без обёрток ```.\n"
        "Поля status: \"ok\" — угроз не обнаружено; "
        "\"warning\" — есть подозрительное, требует внимания; "
        "\"critical\" — критично, требует немедленного исправления.\n"
        "В data укажи: \"summary\" — краткая сводка (1–3 предложения, по-русски); "
        "\"findings\" — массив находок, каждая в формате "
        "{\"title\": \"…\", \"severity\": \"…\", \"details\": \"…\", "
        "\"recommendation\": \"как устранить\"}.\n");
    return prompt;
}

qint64 LlmScheduler::lastAnalysisTime() const
{
    const QVector<LlmJournalEntry> &entries = m_journal.entries();
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
        // Тест-запросы период не двигают
        if (it->source == LlmJournalEntry::Source::Test)
            continue;
        const QDateTime &moment =
            it->finishTime.isValid() ? it->finishTime : it->startTime;
        if (moment.isValid())
            return moment.toMSecsSinceEpoch();
    }
    return 0;
}

void LlmScheduler::sendChunk(const QString &prompt, int anomaliesInPrompt,
                             const QString &logPeriodFromIso,
                             LlmJournalEntry::Source source)
{
    const LlmSettings &llm = Settings::instance().llm();
    m_currentSource = source;
    m_currentEntryId = m_journal.beginEntry(source, llm.model, prompt,
                                            anomaliesInPrompt, logPeriodFromIso);
    m_currentStartedAt = QDateTime::currentMSecsSinceEpoch();
    emit statusChanged(tr("LLM: отправка запроса (%1, аномалий в контексте: %2)…")
                           .arg(llm.model)
                           .arg(anomaliesInPrompt));
    m_client->analyze(llm.systemPrompt, prompt, llm);
}

void LlmScheduler::advanceChunkPipeline()
{
    const int total = m_chunkPrompts.size();
    if (m_chunkIndex >= total) {
        // все чанки отправлены — агрегируем и фиксируем итог
        m_chunkRunActive = false;
        const int analysed = m_chunksCompleted + m_chunksFailed;
        if (m_aggregateLevel == LlmVerdict::Level::Critical
            || m_aggregateLevel == LlmVerdict::Level::Warning) {
            LlmVerdict agg;
            agg.parsed = true;
            agg.level = m_aggregateLevel;
            agg.statusText = LlmVerdict::statusString(m_aggregateLevel);
            agg.summary = m_aggregateSummaries.join(QStringLiteral(" / "));
            agg.findings = m_aggregateFindings;
            emit verdictAnomaly(verdictToAnomaly(agg));
            emit statusChanged(
                tr("LLM: анализ завершён (%1 часть(ей), вердикт: %2)")
                    .arg(analysed)
                    .arg(LlmVerdict::levelName(m_aggregateLevel)));
        } else {
            emit statusChanged(
                tr("LLM: анализ завершён (%1 часть(ей), вердикт: %2)")
                    .arg(analysed)
                    .arg(LlmVerdict::levelName(m_aggregateLevel)));
        }
        emit analysisFinished(true,
                              m_aggregateSummaries.join(QStringLiteral(" / ")).left(300));
        m_chunkPrompts.clear();
        m_chunkAnomalies.clear();
        m_chunkPeriodFrom.clear();
        m_chunkKeys.clear();
        m_inflightKeys.clear();
        return;
    }

    // Инкремент — до отправки: если клиент ответит синхронно, ответчик
    // снова войдёт в эту функцию, и без преждевременного инкремента один
    // и тот же чанк уходил бы рекурсивно бесконечно (переполнение стека).
    const int index = m_chunkIndex++;
    m_inflightKeys = m_chunkKeys.at(index);
    sendChunk(m_chunkPrompts[index], m_chunkAnomalies[index],
              m_chunkPeriodFrom[index], m_currentSource);
}

Anomaly LlmScheduler::verdictToAnomaly(const LlmVerdict &verdict)
{
    Anomaly anomaly;
    anomaly.setTimestamp(QDateTime::currentDateTime());
    anomaly.setRuleName(QObject::tr("Анализ LLM"));
    // В колонке «Обнаружение» — название модели, ответившей вердиктом
    const QString model = Settings::instance().llm().model;
    anomaly.setDetector(model.isEmpty() ? QObject::tr("LLM") : model);
    anomaly.setSeverity(verdict.level == LlmVerdict::Level::Critical
                            ? Anomaly::Severity::Critical
                            : Anomaly::Severity::Warning);
    anomaly.setTitle(verdict.notificationSummary());
    anomaly.setDescription(verdict.detailsText());
    anomaly.setPacketCount(1);
    return anomaly;
}

void LlmScheduler::startAnalysis(LlmJournalEntry::Source source)
{
    if (!m_client) {
        emit statusChanged(tr("LLM: клиент не настроен"));
        return;
    }
    if (isBusy()) {
        emit statusChanged(tr("LLM: предыдущий запрос ещё выполняется"));
        return;
    }
    if (m_chunkRunActive) {
        emit statusChanged(tr("LLM: анализ частями ещё выполняется"));
        return;
    }

    const LlmSettings &llm = Settings::instance().llm();

    // Период журнала событий — с момента предыдущего запроса к LLM
    // (первый запрос за жизнь журнала — за последние 15 минут).
    qint64 sinceMs = lastAnalysisTime();
    if (sinceMs <= 0)
        sinceMs = QDateTime::currentMSecsSinceEpoch() - 15 * 60 * 1000;
    const QString periodFromIso =
        QDateTime::fromMSecsSinceEpoch(sinceMs).toString(Qt::ISODate);

    // Контекст: только новые аномалии (уже отправленные в LLM не повторяем).
    int skippedAlreadySent = 0;
    const QVector<QJsonObject> anomalies =
        newAnomaliesForPrompt(&skippedAlreadySent);
    const QString prompt =
        buildPrompt(anomalies, skippedAlreadySent, sinceMs, periodFromIso);

    // Разбивка контекста на чанки: если промпт превышает бюджет токенов,
    // журнал аномалий режется на части, каждая шлётся отдельным запросом,
    // а вердикты агрегируются (максимум по уровню + конкатенация находок).
    const QString sysPrompt = llm.systemPrompt;
    const int budget = PromptBudget::clampBudget(llm.contextBudgetTokens);
    const int promptTokens = PromptBudget::estimateTokens(prompt, sysPrompt);

    if (promptTokens <= budget) {
        m_chunkRunActive = false;
        m_inflightKeys = anomalyKeys(anomalies);
        sendChunk(prompt, anomalies.size(), periodFromIso, source);
        return;
    }

    // Преамбула (сводка + журнал событий + инструкции) — общий хвост промпта,
    // журнал аномалий вырезаем из него и режем на чанки.
    static const QString kAnomaliesHeader =
        QStringLiteral("\n=== ОБНАРУЖЕННЫЕ АНОМАЛИИ");
    const int headerAt = prompt.indexOf(kAnomaliesHeader);
    QString preamble;
    QString anomaliesJson;
    QString epilogue;
    if (headerAt >= 0) {
        const int eventsAt = prompt.indexOf(
            QStringLiteral("\n=== ЖУРНАЛ СОБЫТИЙ"), headerAt);
        preamble = prompt.left(headerAt);
        if (eventsAt > headerAt) {
            anomaliesJson = prompt.mid(headerAt, eventsAt - headerAt);
            epilogue = prompt.mid(eventsAt);
        } else {
            anomaliesJson = prompt.mid(headerAt);
        }
    } else {
        preamble = prompt;   // не вырезалось — режем целиком снизу
    }

    const int reserved = PromptBudget::estimateTokens(preamble + epilogue, sysPrompt);
    const QVector<QVector<QJsonObject>> chunks =
        PromptBudget::splitIntoChunks(anomalies, budget, reserved);

    if (chunks.size() <= 1) {
        // влезло целиком (или аномалий мало) — один запрос
        m_chunkRunActive = false;
        m_inflightKeys = anomalyKeys(anomalies);
        sendChunk(prompt, anomalies.size(), periodFromIso, source);
        return;
    }

    m_chunkRunActive = true;
    m_currentSource = source;
    m_chunkIndex = 0;
    m_chunksCompleted = 0;
    m_chunksFailed = 0;
    m_aggregateLevel = LlmVerdict::Level::Unknown;
    m_aggregateSummaries.clear();
    m_aggregateFindings.clear();
    m_aggregateElapsed = 0;
    m_chunkPrompts.clear();
    m_chunkAnomalies.clear();
    m_chunkPeriodFrom.clear();
    m_chunkKeys.clear();
    m_inflightKeys.clear();

    for (const QVector<QJsonObject> &chunk : chunks) {
        QJsonArray array;
        for (const QJsonObject &item : chunk)
            array.append(item);
        const QString chunkAnoms =
            QStringLiteral("\n=== ОБНАРУЖЕННЫЕ АНОМАЛИИ (часть %1 из %2, всего %3) ===\n")
                .arg(m_chunkPrompts.size() + 1)
                .arg(chunks.size())
                .arg(anomalies.size())
            + QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Indented))
            + QStringLiteral("\nПоле \"status\": \"resolved\" — аномалия уже отмечена "
                             "оператором решённой, повторно её анализировать не нужно.\n");
        m_chunkPrompts.append(preamble + chunkAnoms + epilogue);
        m_chunkAnomalies.append(chunk.size());
        m_chunkPeriodFrom.append(periodFromIso);
        m_chunkKeys.append(anomalyKeys(chunk));
    }

    emit statusChanged(tr("LLM: контекст превышает бюджет (%1 токенов), "
                          "отправка %2 частями…").arg(promptTokens).arg(chunks.size()));
    advanceChunkPipeline();
}
