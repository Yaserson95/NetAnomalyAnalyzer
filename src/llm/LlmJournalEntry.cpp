#include "LlmJournalEntry.h"

#include <QObject>

QString LlmJournalEntry::statusName(Status status)
{
    switch (status) {
    case Status::Pending: return QObject::tr("Выполняется");
    case Status::Success: return QObject::tr("Успешно");
    case Status::Error:   return QObject::tr("Ошибка");
    }
    return {};
}

QString LlmJournalEntry::sourceName(Source source)
{
    switch (source) {
    case Source::Scheduled: return QObject::tr("По расписанию");
    case Source::Manual:    return QObject::tr("По запросу");
    case Source::Test:      return QObject::tr("Тест");
    }
    return {};
}

QString LlmJournalEntry::verdictName(const QString &verdictStatus)
{
    if (verdictStatus == QLatin1String("ok"))
        return QObject::tr("Норма");
    if (verdictStatus == QLatin1String("warning"))
        return QObject::tr("Внимание");
    if (verdictStatus == QLatin1String("critical"))
        return QObject::tr("Критично");
    if (verdictStatus.isEmpty())
        return QObject::tr("—");
    return verdictStatus;
}

qint64 LlmJournalEntry::durationSec() const
{
    if (elapsedMs > 0)
        return elapsedMs / 1000;
    if (startTime.isValid() && finishTime.isValid())
        return startTime.msecsTo(finishTime) / 1000;
    return 0;
}

QString LlmJournalEntry::shortResponse(int maxLen) const
{
    QString text = response.simplified();
    if (text.size() > maxLen)
        text = text.left(maxLen) + QStringLiteral("…");
    return text;
}

QJsonObject LlmJournalEntry::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("source"), sourceName(source));
    object.insert(QStringLiteral("status"), statusName(status));
    object.insert(QStringLiteral("startTime"), startTime.toString(Qt::ISODate));
    object.insert(QStringLiteral("finishTime"), finishTime.toString(Qt::ISODate));
    object.insert(QStringLiteral("model"), model);
    object.insert(QStringLiteral("elapsedMs"), elapsedMs);
    object.insert(QStringLiteral("anomaliesInPrompt"), anomaliesInPrompt);
    object.insert(QStringLiteral("promptPreview"), promptPreview);
    if (!prompt.isEmpty())
        object.insert(QStringLiteral("prompt"), prompt);
    if (!logPeriodFrom.isEmpty())
        object.insert(QStringLiteral("logPeriodFrom"), logPeriodFrom);
    if (!verdictStatus.isEmpty())
        object.insert(QStringLiteral("verdictStatus"), verdictStatus);
    if (!verdictData.isEmpty())
        object.insert(QStringLiteral("verdictData"), verdictData);
    if (!response.isEmpty())
        object.insert(QStringLiteral("response"), response);
    if (!error.isEmpty())
        object.insert(QStringLiteral("error"), error);
    return object;
}

LlmJournalEntry LlmJournalEntry::fromJson(const QJsonObject &object)
{
    LlmJournalEntry entry;
    entry.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
    const QString status = object.value(QStringLiteral("status")).toString();
    if (status == statusName(Status::Success))
        entry.status = Status::Success;
    else if (status == statusName(Status::Error))
        entry.status = Status::Error;
    else
        entry.status = Status::Pending;
    const QString source = object.value(QStringLiteral("source")).toString();
    if (source == sourceName(Source::Scheduled))
        entry.source = Source::Scheduled;
    else if (source == sourceName(Source::Test))
        entry.source = Source::Test;
    else
        entry.source = Source::Manual;
    entry.startTime = QDateTime::fromString(
        object.value(QStringLiteral("startTime")).toString(), Qt::ISODate);
    entry.finishTime = QDateTime::fromString(
        object.value(QStringLiteral("finishTime")).toString(), Qt::ISODate);
    entry.model = object.value(QStringLiteral("model")).toString();
    entry.elapsedMs =
        static_cast<qint64>(object.value(QStringLiteral("elapsedMs")).toDouble());
    entry.anomaliesInPrompt = object.value(QStringLiteral("anomaliesInPrompt")).toInt();
    entry.promptPreview = object.value(QStringLiteral("promptPreview")).toString();
    entry.prompt = object.value(QStringLiteral("prompt")).toString();
    entry.logPeriodFrom = object.value(QStringLiteral("logPeriodFrom")).toString();
    entry.verdictStatus = object.value(QStringLiteral("verdictStatus")).toString();
    entry.verdictData = object.value(QStringLiteral("verdictData")).toObject();
    entry.response = object.value(QStringLiteral("response")).toString();
    entry.error = object.value(QStringLiteral("error")).toString();
    return entry;
}
