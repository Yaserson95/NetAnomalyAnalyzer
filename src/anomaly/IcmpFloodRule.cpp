#include "IcmpFloodRule.h"

#include <QDateTime>

IcmpFloodRule::IcmpFloodRule(const DetectionSettings *settings)
    : AnomalyRule(settings)
{
    m_window.setWindowMs(1000);
}

std::optional<Anomaly> IcmpFloodRule::check(const Packet &packet)
{
    if (!settings().enableIcmpFlood ||
        (packet.transport() != Packet::Transport::ICMP &&
         packet.transport() != Packet::Transport::ICMPv6))
        return std::nullopt;

    const qint64 now = packet.timestamp().toMSecsSinceEpoch();
    const QString key = packet.interfaceName() + QLatin1Char('|') + packet.sourceIp();
    m_window.add(key, now);

    const int rate = m_window.count(key, now);
    if (rate < settings().icmpFloodPps)
        return std::nullopt;

    Anomaly anomaly = makeAnomaly(packet, Anomaly::Severity::Warning,
                                  QStringLiteral("Возможный ICMP-флуд"),
                                  QStringLiteral("Источник %1 отправил %2 ICMP-пакетов за секунду "
                                                 "(порог %3)")
                                      .arg(packet.sourceIp())
                                      .arg(rate)
                                      .arg(settings().icmpFloodPps));
    anomaly.setSourceIp(packet.sourceIp());
    anomaly.setDestinationIp(packet.destinationIp());
    anomaly.setPacketCount(rate);
    m_window.remove(key);
    return anomaly;
}

void IcmpFloodRule::reset()
{
    m_window.clear();
}
