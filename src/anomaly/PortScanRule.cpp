#include "PortScanRule.h"

#include <QDateTime>
#include <iterator>

PortScanRule::PortScanRule(const DetectionSettings *settings)
    : AnomalyRule(settings)
{
}

std::optional<Anomaly> PortScanRule::check(const Packet &packet)
{
    if (!settings().enablePortScan)
        return std::nullopt;
    if (packet.transport() != Packet::Transport::TCP || packet.destinationPort() == 0)
        return std::nullopt;

    const qint64 now = packet.timestamp().toMSecsSinceEpoch();
    const qint64 windowMs = static_cast<qint64>(settings().portScanWindowSec) * 1000;
    const QString src = packet.sourceIp();
    const QString dst = packet.destinationIp();

    Target &target = m_sources[src][dst];
    target.ports.insert(packet.destinationPort(), now);

    // Удаляем порты, которые не трогали дольше окна.
    for (auto it = target.ports.begin(); it != target.ports.end();) {
        it = (now - it.value() > windowMs) ? target.ports.erase(it) : std::next(it);
    }

    if (target.ports.size() < settings().portScanPortThreshold)
        return std::nullopt;

    QStringList ports;
    const auto sortedPorts = target.ports.keys();
    for (quint16 port : sortedPorts)
        ports << QString::number(port);
    ports = ports.mid(0, 20);

    Anomaly anomaly = makeAnomaly(packet, Anomaly::Severity::Critical,
                                  QStringLiteral("Обнаружено сканирование портов"),
                                  QStringLiteral("Источник %1 проверил %2 портов у хоста %3 "
                                                 "за %4 сек. Порты: %5")
                                      .arg(src)
                                      .arg(target.ports.size())
                                      .arg(dst)
                                      .arg(settings().portScanWindowSec)
                                      .arg(ports.join(QLatin1String(", "))));
    anomaly.setSourceIp(src);
    anomaly.setDestinationIp(dst);
    anomaly.setPacketCount(target.ports.size());

    target.ports.clear(); // не дублируем то же сканирование
    return anomaly;
}

void PortScanRule::reset()
{
    m_sources.clear();
}
