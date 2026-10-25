#pragma once

#include "LlmVerdict.h"

#include <QString>
#include <QVector>

class QJsonObject;

/**
 * @brief Оценка размера текста в токенах и деление запроса на части.
 *
 * Точная токенизация недоступна (формат зависит от модели), поэтому размер
 * оцениваем приближённо: ~4 символа на токен для смешанной латиницы/кириллицы
 * (кириллица в most токенизаторах занимает ~1 байт/символ, ~3 символа на
 * токен; берём консервативные 4 символа, чтобы не слать заведомо превышающие
 * запросы, и оставляем запас на служебные поля).
 */
class PromptBudget {
public:
    /**
     * Оценить число токенов в тексте (приближённо, символы/4).
     * @param text          текст для оценки;
     * @param systemPrompt  системный промпт, который тоже занимает место
     *                      в контексте (может быть пустым).
     */
    static int estimateTokens(const QString &text, const QString &systemPrompt = QString());

    /** Приблизительное число токенов в одной JSON-записи журнала. */
    static int estimateRecordTokens(const QJsonObject &record);

    /**
     * Разбить JSON-записи на чанки так, чтобы каждый (вместе с шапкой) укладывался
     * в бюджет. Шапка — фиксированная преамбула (сводка + инструкции), она
     * вычитается из бюджета один раз на чанк.
     *
     * @param records       записи для отправки (журнал аномалий/событий);
     * @param budgetTokens общий бюджет контекста в токенах;
     * @param reservedTokens токены, занятые шапкой и системным промптом;
     * @return              минимум один чанк (пустой, если records пусты).
     */
    static QVector<QVector<QJsonObject>> splitIntoChunks(
        const QVector<QJsonObject> &records, int budgetTokens, int reservedTokens);

    /** Минимальный бюджет контекста (защита от нуля/отрицательных значений). */
    static int clampBudget(int budgetTokens);
};

/**
 * @brief Агрегация вердиктов нескольких чанков в итоговый.
 *
 * Правила: итоговый уровень — максимальный по чанкам (critical > warning > ok),
 * summary — сводная строка по чанкам, findings — конкатенация всех находок.
 */
class VerdictAggregator {
public:
    /** Добавить разобранный вердикт чанка (нераспознанные игнорируются). */
    void add(const LlmVerdict &verdict);

    bool hasAny() const { return m_hasAny; }
    LlmVerdict::Level level() const { return m_level; }
    /** Итоговый вердикт для журнала/оповещения (пустой data.summary, если нечего). */
    LlmVerdict aggregated() const;
    /** Число учтённых чанков. */
    int count() const { return m_count; }

private:
    bool m_hasAny = false;
    int m_count = 0;
    LlmVerdict::Level m_level = LlmVerdict::Level::Ok;
    QStringList m_summaries;
    QVector<LlmVerdict::Finding> m_findings;
};
