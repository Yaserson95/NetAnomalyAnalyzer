#include "BroadcastStormRule.h"

#include <QDateTime>

BroadcastStormRule::BroadcastStormRule(const DetectionSettings *settings)
    : AnomalyRule(settings)
{
    m_window.setWindowMs(1000);
}

std::optional<Anomaly> BroadcastStormRule::check(const Packet &packet)
{
    if (!settings().enableBroadcast)
        return std::nullopt;
    if (!packet.isBroadcast() && !packet.isMulticast())
        return std::nullopt;

    const qint64 now = packet.timestamp().toMSecsSinceEpoch();
    const QString key = packet.interfaceName();
    m_window.add(key, now);

    const int rate = m_window.count(key, now);
    if (rate < settings().broadcastPps)
        return std::nullopt;

    Anomaly anomaly = makeAnomaly(packet, Anomaly::Severity::Warning,
                                  QStringLiteral("Аномально много broadcast-трафика"),
                                  QStringLiteral("Интерфейс %1: %2 широковещательных/мультикастовых "
                                                 "пакетов за секунду (порог %3)")
                                      .arg(packet.interfaceName())
                                      .arg(rate)
                                      .arg(settings().broadcastPps));
    anomaly.setDestinationIp(packet.destinationIp());
    anomaly.setPacketCount(rate);
    m_window.remove(key);
    return anomaly;
}

void BroadcastStormRule::reset()
{
    m_window.clear();
}
