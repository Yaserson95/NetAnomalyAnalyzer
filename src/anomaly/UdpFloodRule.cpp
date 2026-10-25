#include "UdpFloodRule.h"

#include <QDateTime>

UdpFloodRule::UdpFloodRule(const DetectionSettings *settings)
    : AnomalyRule(settings)
{
    m_window.setWindowMs(1000);
}

std::optional<Anomaly> UdpFloodRule::check(const Packet &packet)
{
    if (!settings().enableUdpFlood || packet.transport() != Packet::Transport::UDP)
        return std::nullopt;

    const qint64 now = packet.timestamp().toMSecsSinceEpoch();
    const QString key = packet.interfaceName() + QLatin1Char('|') + packet.destinationIp();
    m_window.add(key, now);

    const int rate = m_window.count(key, now);
    if (rate < settings().udpFloodPps)
        return std::nullopt;

    Anomaly anomaly = makeAnomaly(packet, Anomaly::Severity::Warning,
                                  QStringLiteral("Возможный UDP-флуд"),
                                  QStringLiteral("К хосту %1 за 1 секунду пришло %2 UDP-пакетов "
                                                 "(порог %3)")
                                      .arg(packet.destinationIp())
                                      .arg(rate)
                                      .arg(settings().udpFloodPps));
    anomaly.setSourceIp(packet.sourceIp());
    anomaly.setDestinationIp(packet.destinationIp());
    anomaly.setDestinationPort(packet.destinationPort());
    anomaly.setPacketCount(rate);
    m_window.remove(key);
    return anomaly;
}

void UdpFloodRule::reset()
{
    m_window.clear();
}
