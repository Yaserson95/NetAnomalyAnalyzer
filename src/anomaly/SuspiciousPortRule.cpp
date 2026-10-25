#include "SuspiciousPortRule.h"

#include <QDateTime>

SuspiciousPortRule::SuspiciousPortRule(const DetectionSettings *settings)
    : AnomalyRule(settings)
{
}

QSet<quint16> SuspiciousPortRule::cachedPorts() const
{
    QSet<quint16> ports;
    for (const QString &value : settings().suspiciousPorts) {
        bool ok = false;
        const uint port = value.trimmed().toUInt(&ok);
        if (ok && port <= 65535)
            ports.insert(static_cast<quint16>(port));
    }
    return ports;
}

std::optional<Anomaly> SuspiciousPortRule::check(const Packet &packet)
{
    if (!settings().enableSuspiciousPort)
        return std::nullopt;
    if (packet.destinationPort() == 0)
        return std::nullopt;

    m_ports = cachedPorts();
    if (!m_ports.contains(packet.destinationPort()))
        return std::nullopt;

    // Не спамим: одна и та же пара src->dst:port — не чаще cooldown.
    const qint64 now = packet.timestamp().toMSecsSinceEpoch();
    const QString key = packet.sourceIp() + QLatin1Char('|') +
                        packet.destinationIp() + QLatin1Char('|') +
                        QString::number(packet.destinationPort());
    if (key == m_lastKey && now - m_lastEmitMs <
                                static_cast<qint64>(settings().anomalyCooldownSec) * 1000)
        return std::nullopt;
    m_lastKey = key;
    m_lastEmitMs = now;

    Anomaly anomaly = makeAnomaly(packet, Anomaly::Severity::Critical,
                                  QStringLiteral("Трафик на подозрительном порту %1")
                                      .arg(packet.destinationPort()),
                                  QStringLiteral("Соединение %1:%2 → %3:%4 использует порт, "
                                                 "часто применяемый бэкдорами")
                                      .arg(packet.sourceIp())
                                      .arg(packet.sourcePort())
                                      .arg(packet.destinationIp())
                                      .arg(packet.destinationPort()));
    anomaly.setSourceIp(packet.sourceIp());
    anomaly.setDestinationIp(packet.destinationIp());
    anomaly.setSourcePort(packet.sourcePort());
    anomaly.setDestinationPort(packet.destinationPort());
    anomaly.setSample(packet.summary());
    return anomaly;
}

void SuspiciousPortRule::reset()
{
    m_lastKey.clear();
    m_lastEmitMs = 0;
}
