#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

/**
 * @brief Разобранный вердикт LLM в формате {"status":…, "data":{…}}.
 *
 * Модель просится отвечать строго таким JSON-объектом:
 *   status — "ok" (угроз нет), "warning" (требует внимания),
 *            "critical" (нужно немедленно исправить);
 *   data   — подробности: summary + массив findings
 *            (title, severity, details, recommendation).
 *
 * Разбор терпим к оформлению: убирает ```-обёртки и текст вокруг JSON.
 */
class LlmVerdict {
public:
    enum class Level { Unknown, Ok, Warning, Critical };

    struct Finding {
        QString title;
        QString severity;
        QString details;
        QString recommendation;
    };

    bool         parsed = false;    ///< JSON-объект извлечён из ответа
    Level        level = Level::Unknown;
    QString      statusText;        ///< сырое значение status (строчными)
    QString      summary;           ///< data.summary
    QVector<Finding> findings;      ///< data.findings
    QJsonObject  data;              ///< сырой data целиком
    QString      rawJson;           ///< извлечённый JSON-фрагмент

    /** Извлечь и разобрать вердикт из текстового ответа модели. */
    static LlmVerdict fromResponse(const QString &text);
    /** Уровень по строковому значению status. */
    static Level levelFromStatus(const QString &status);
    /** Сырое строковое значение status по уровню ("ok"/"warning"/"critical"/""). */
    static QString statusString(Level level);
    /** Человекочитаемое имя уровня («Норма»/«Внимание»/«Критично»/«—»). */
    static QString levelName(Level level);
    static QString levelName(const QString &status);

    /** Однострочная сводка для оповещений. */
    QString notificationSummary() const;
    /** Многострочная сводка (находки с рекомендациями) для панели/описания. */
    QString detailsText() const;
};
