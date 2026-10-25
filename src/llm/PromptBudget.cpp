#include "PromptBudget.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

int PromptBudget::clampBudget(int budgetTokens)
{
    // Минимум, чтобы поместилась хоть какая-то полезная часть запроса.
    constexpr int kMinBudget = 500;
    return budgetTokens < kMinBudget ? kMinBudget : budgetTokens;
}

int PromptBudget::estimateTokens(const QString &text, const QString &systemPrompt)
{
    // Приближение «~4 символа на токен». Кириллица плотнее (≈3 символа/токен),
    // берём консервативно, чтобы не слать заведомо превышающие запросы.
    const qlonglong total = qlonglong(text.size()) + qlonglong(systemPrompt.size());
    return static_cast<int>((total + 3) / 4);
}

int PromptBudget::estimateRecordTokens(const QJsonObject &record)
{
    // Компактная сериализация — близко к тому, что реально уйдёт в промпт.
    const QByteArray compact = QJsonDocument(record).toJson(QJsonDocument::Compact);
    return static_cast<int>((compact.size() + 3) / 4);
}

QVector<QVector<QJsonObject>> PromptBudget::splitIntoChunks(
    const QVector<QJsonObject> &records, int budgetTokens, int reservedTokens)
{
    QVector<QVector<QJsonObject>> chunks;
    if (records.isEmpty()) {
        chunks.append(QVector<QJsonObject>());
        return chunks;
    }

    const int budget = clampBudget(budgetTokens);
    // Полезная ёмкость одного чанка после вычета шапки/системного промпта.
    const int usable = budget - reservedTokens;
    const int perChunk = usable < 64 ? 64 : usable;   // защита от вырождения

    QVector<QJsonObject> current;
    int currentTokens = 0;
    for (const QJsonObject &record : records) {
        const int cost = estimateRecordTokens(record) + 1;   // +1 — перевод строки
        // Запись одна не влезает даже целиком — кладём в отдельный чанк
        // (лучше отправить её заведомо большой, чем выбросить).
        if (!current.isEmpty() && currentTokens + cost > perChunk) {
            chunks.append(current);
            current.clear();
            currentTokens = 0;
        }
        current.append(record);
        currentTokens += cost;
    }
    if (!current.isEmpty())
        chunks.append(current);
    if (chunks.isEmpty())
        chunks.append(QVector<QJsonObject>());
    return chunks;
}

void VerdictAggregator::add(const LlmVerdict &verdict)
{
    if (!verdict.parsed)
        return;
    m_hasAny = true;
    ++m_count;
    if (verdict.level == LlmVerdict::Level::Critical)
        m_level = LlmVerdict::Level::Critical;
    else if (verdict.level == LlmVerdict::Level::Warning
             && m_level != LlmVerdict::Level::Critical)
        m_level = LlmVerdict::Level::Warning;
    if (!verdict.summary.isEmpty())
        m_summaries.append(verdict.summary);
    m_findings += verdict.findings;
}

LlmVerdict VerdictAggregator::aggregated() const
{
    LlmVerdict result;
    if (!m_hasAny)
        return result;

    result.parsed = true;
    result.level = m_level;
    result.statusText = LlmVerdict::statusString(m_level);
    result.findings = m_findings;
    result.summary = m_summaries.join(QLatin1Char(' '));

    // Собираем data: summary + findings (массив), чтобы журнал и оповещение
    // получили ту же структуру, что и у одиночного ответа.
    result.data.insert(QStringLiteral("summary"), result.summary);
    QJsonArray findings;
    for (const LlmVerdict::Finding &finding : std::as_const(m_findings)) {
        QJsonObject item;
        item.insert(QStringLiteral("title"), finding.title);
        item.insert(QStringLiteral("severity"), finding.severity);
        item.insert(QStringLiteral("details"), finding.details);
        item.insert(QStringLiteral("recommendation"), finding.recommendation);
        findings.append(item);
    }
    result.data.insert(QStringLiteral("findings"), findings);
    return result;
}
