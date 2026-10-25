#include "AnomalyDetector.h"

AnomalyDetector::AnomalyDetector(const DetectionSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_rules(createDefaultRules(settings))
{
    qRegisterMetaType<Anomaly>("Anomaly");
}

void AnomalyDetector::onPacket(const Packet &packet)
{
    for (auto &rule : m_rules) {
        std::optional<Anomaly> anomaly = rule->check(packet);
        if (!anomaly)
            continue;

        const qint64 now = packet.timestamp().toMSecsSinceEpoch();
        if (isSuppressed(*anomaly, now))
            continue;

        anomaly->setId(m_nextId++);
        anomaly->setDetector(Anomaly::defaultDetectorName());
        if (anomaly->timestamp().isNull())
            anomaly->setTimestamp(packet.timestamp());

        ++m_total;
        emit anomalyDetected(*anomaly);
        emit totalAnomaliesChanged(m_total);
    }
}

bool AnomalyDetector::isSuppressed(const Anomaly &anomaly, qint64 nowMs)
{
    const QString key = anomaly.ruleName() + QLatin1Char('|') +
                        anomaly.sourceIp() + QLatin1Char('|') + anomaly.destinationIp();
    const qint64 cooldownMs = static_cast<qint64>(m_settings->anomalyCooldownSec) * 1000;

    const auto it = m_lastEmitted.constFind(key);
    if (it != m_lastEmitted.constEnd() && nowMs - it.value() < cooldownMs)
        return true;

    m_lastEmitted.insert(key, nowMs);
    if (m_lastEmitted.size() > 4096)
        m_lastEmitted.clear();
    return false;
}

void AnomalyDetector::resetRules()
{
    for (auto &rule : m_rules)
        rule->reset();
    m_lastEmitted.clear();
}
