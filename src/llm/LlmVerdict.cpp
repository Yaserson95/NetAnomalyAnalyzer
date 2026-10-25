#include "LlmVerdict.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>

namespace {
/** Убрать ```-обёртку markdown, если модель её добавила. */
QString stripFences(const QString &text)
{
    QString trimmed = text.trimmed();
    const int fence = trimmed.indexOf(QLatin1String("```"));
    if (fence >= 0) {
        int start = trimmed.indexOf(QLatin1Char('\n'), fence);
        start = (start >= 0) ? start + 1 : fence;
        const int end = trimmed.lastIndexOf(QLatin1String("```"));
        if (end > start)
            trimmed = trimmed.mid(start, end - start).trimmed();
    }
    return trimmed;
}
} // namespace

LlmVerdict LlmVerdict::fromResponse(const QString &text)
{
    LlmVerdict verdict;

    // Извлечь первый '{' … последний '}' (к тексту вокруг JSON относимся
    // терпимо — модель может добавить вводную фразу)
    const QString cleaned = stripFences(text);
    const int start = cleaned.indexOf(QLatin1Char('{'));
    const int end = cleaned.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start)
        return verdict;

    const QString json = cleaned.mid(start, end - start + 1);
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return verdict;

    const QJsonObject root = doc.object();
    verdict.parsed = true;
    verdict.rawJson = json;
    verdict.statusText =
        root.value(QStringLiteral("status")).toString().trimmed().toLower();
    verdict.level = levelFromStatus(verdict.statusText);
    verdict.data = root.value(QStringLiteral("data")).toObject();
    verdict.summary = verdict.data.value(QStringLiteral("summary")).toString();

    const QJsonArray findings = verdict.data.value(QStringLiteral("findings")).toArray();
    for (const QJsonValue &value : findings) {
        const QJsonObject item = value.toObject();
        Finding finding;
        finding.title = item.value(QStringLiteral("title")).toString();
        finding.severity = item.value(QStringLiteral("severity")).toString();
        finding.details = item.value(QStringLiteral("details")).toString();
        finding.recommendation =
            item.value(QStringLiteral("recommendation")).toString();
        verdict.findings.append(finding);
    }
    return verdict;
}

LlmVerdict::Level LlmVerdict::levelFromStatus(const QString &status)
{
    if (status == QLatin1String("ok"))
        return Level::Ok;
    if (status == QLatin1String("warning"))
        return Level::Warning;
    if (status == QLatin1String("critical"))
        return Level::Critical;
    return Level::Unknown;
}

QString LlmVerdict::levelName(Level level)
{
    switch (level) {
    case Level::Ok:       return QObject::tr("Норма");
    case Level::Warning:  return QObject::tr("Внимание");
    case Level::Critical: return QObject::tr("Критично");
    case Level::Unknown:  return QObject::tr("—");
    }
    return {};
}

QString LlmVerdict::statusString(Level level)
{
    switch (level) {
    case Level::Ok:       return QStringLiteral("ok");
    case Level::Warning:  return QStringLiteral("warning");
    case Level::Critical: return QStringLiteral("critical");
    case Level::Unknown:  return {};
    }
    return {};
}

QString LlmVerdict::levelName(const QString &status)
{
    return levelName(levelFromStatus(status));
}

QString LlmVerdict::notificationSummary() const
{
    if (!summary.isEmpty())
        return summary;
    if (!findings.isEmpty())
        return findings.first().title;
    return QObject::tr("LLM: промежуточный анализ сети");
}

QString LlmVerdict::detailsText() const
{
    QString text;
    if (!summary.isEmpty())
        text += summary + QLatin1Char('\n');
    for (const Finding &finding : findings) {
        text += QStringLiteral("\n— %1").arg(finding.title);
        if (!finding.severity.isEmpty())
            text += QStringLiteral(" [%1]").arg(finding.severity);
        text += QLatin1Char('\n');
        if (!finding.details.isEmpty())
            text += QStringLiteral("  %1\n").arg(finding.details);
        if (!finding.recommendation.isEmpty())
            text += QStringLiteral("  Рекомендация: %1\n")
                        .arg(finding.recommendation);
    }
    return text;
}
