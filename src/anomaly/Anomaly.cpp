#include "Anomaly.h"

#include <QJsonArray>
#include <QObject>

QString Anomaly::severityName(Severity severity)
{
    switch (severity) {
    case Severity::Info:     return QObject::tr("Информация");
    case Severity::Warning:  return QObject::tr("Предупреждение");
    case Severity::Critical: return QObject::tr("Критично");
    }
    return {};
}

QString Anomaly::defaultDetectorName()
{
    return QStringLiteral("Net Anomaly Analyzer");
}

QString Anomaly::notificationText() const
{
    QString text = QStringLiteral("[%1] %2 — %3").arg(severityName(), m_ruleName, m_title);
    if (!m_sourceIp.isEmpty())
        text += QStringLiteral(" (%1").arg(m_sourceIp);
    if (!m_destinationIp.isEmpty())
        text += QStringLiteral(" → %1)").arg(m_destinationIp);
    return text;
}

QJsonObject Anomaly::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), m_id);
    object.insert(QStringLiteral("timestamp"), m_timestamp.toString(Qt::ISODate));
    object.insert(QStringLiteral("interface"), m_interfaceName);
    object.insert(QStringLiteral("rule"), m_ruleName);
    if (!m_detector.isEmpty())
        object.insert(QStringLiteral("detector"), m_detector);
    object.insert(QStringLiteral("severity"), severityName());
    object.insert(QStringLiteral("title"), m_title);
    object.insert(QStringLiteral("description"), m_description);
    object.insert(QStringLiteral("sourceIp"), m_sourceIp);
    object.insert(QStringLiteral("destinationIp"), m_destinationIp);
    if (m_sourcePort)
        object.insert(QStringLiteral("sourcePort"), m_sourcePort);
    if (m_destinationPort)
        object.insert(QStringLiteral("destinationPort"), m_destinationPort);
    object.insert(QStringLiteral("packetCount"), m_packetCount);
    if (!m_sample.isEmpty())
        object.insert(QStringLiteral("sample"), m_sample);
    return object;
}

Anomaly Anomaly::fromJson(const QJsonObject &object)
{
    Anomaly anomaly;
    anomaly.setId(object.value(QStringLiteral("id")).toInt());
    const QString ts = object.value(QStringLiteral("timestamp")).toString();
    if (!ts.isEmpty())
        anomaly.setTimestamp(QDateTime::fromString(ts, Qt::ISODate));
    anomaly.setInterfaceName(object.value(QStringLiteral("interface")).toString());
    anomaly.setRuleName(object.value(QStringLiteral("rule")).toString());
    anomaly.setDetector(object.value(QStringLiteral("detector")).toString());
    // Уровень в журнале хранится человекочитаемо (см. severityName)
    const QString severity = object.value(QStringLiteral("severity")).toString();
    if (severity == severityName(Severity::Critical))
        anomaly.setSeverity(Severity::Critical);
    else if (severity == severityName(Severity::Warning))
        anomaly.setSeverity(Severity::Warning);
    else
        anomaly.setSeverity(Severity::Info);
    anomaly.setTitle(object.value(QStringLiteral("title")).toString());
    anomaly.setDescription(object.value(QStringLiteral("description")).toString());
    anomaly.setSourceIp(object.value(QStringLiteral("sourceIp")).toString());
    anomaly.setDestinationIp(object.value(QStringLiteral("destinationIp")).toString());
    anomaly.setSourcePort(
        static_cast<quint16>(object.value(QStringLiteral("sourcePort")).toInt()));
    anomaly.setDestinationPort(
        static_cast<quint16>(object.value(QStringLiteral("destinationPort")).toInt()));
    anomaly.setPacketCount(object.value(QStringLiteral("packetCount")).toInt(1));
    anomaly.setSample(object.value(QStringLiteral("sample")).toString());
    return anomaly;
}
