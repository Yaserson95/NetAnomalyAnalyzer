#include "SynFloodRule.h"

#include <QDateTime>

SynFloodRule::SynFloodRule(const DetectionSettings *settings)
    : AnomalyRule(settings)
{
    m_window.setWindowMs(1000);
}

std::optional<Anomaly> SynFloodRule::check(const Packet &packet)
{
    if (!settings().enableSynFlood || !packet.isTcpSyn())
        return std::nullopt;

    const qint64 now = packet.timestamp().toMSecsSinceEpoch();
    const QString key = packet.interfaceName() + QLatin1Char('|') + packet.destinationIp();
    m_window.add(key, now);

    const int rate = m_window.count(key, now);
    if (rate < settings().synFloodPps)
        return std::nullopt;

    Anomaly anomaly = makeAnomaly(packet, Anomaly::Severity::Critical,
                                  QStringLiteral("Возможный SYN-флуд"),
                                  QStringLiteral("К хосту %1 за 1 секунду пришло %2 SYN-пакетов "
                                                 "(порог %3), интерфейс %4")
                                      .arg(packet.destinationIp())
                                      .arg(rate)
                                      .arg(settings().synFloodPps)
                                      .arg(packet.interfaceName()));
    anomaly.setSourceIp(packet.sourceIp());
    anomaly.setDestinationIp(packet.destinationIp());
    anomaly.setDestinationPort(packet.destinationPort());
    anomaly.setPacketCount(rate);
    m_window.remove(key);
    return anomaly;
}

void SynFloodRule::reset()
{
    m_window.clear();
}
