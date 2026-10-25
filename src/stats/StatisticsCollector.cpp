#include "StatisticsCollector.h"
#include "../core/CaptureDevice.h"

#include <QDateTime>
#include <QJsonArray>
#include <QTimer>

StatisticsCollector::StatisticsCollector(QObject *parent)
    : QObject(parent)
{
    auto *timer = new QTimer(this);
    timer->setInterval(kIntervalMs);
    connect(timer, &QTimer::timeout, this, [this] {
        flushRates();
        emit tick();
        emit statsChanged();
    });
    timer->start();
}

StatisticsCollector::InterfaceStats &StatisticsCollector::ensureInterface(const QString &name)
{
    InterfaceStats &stats = m_interfaces[name];
    if (stats.name.isEmpty())
        stats.name = name;
    if (stats.history.isEmpty()) {
        stats.history.fill(0.0, 1);
        stats.historyKbps.fill(0.0, 1);
    }
    return stats;
}

void StatisticsCollector::onPacket(const Packet &packet)
{
    ++m_totalPackets;
    m_totalBytes += packet.capturedLength();

    InterfaceStats &stats = ensureInterface(packet.interfaceName());
    ++stats.packets;
    stats.bytes += packet.capturedLength();

    ++m_pendingPackets[packet.interfaceName()];
    m_pendingBytes[packet.interfaceName()] += packet.capturedLength();
    ++m_protocols[packet.protocolName()];
}

void StatisticsCollector::onAnomaly(const Anomaly &anomaly)
{
    ++m_totalAnomalies;
    ++m_severities[anomaly.severityName()];
    ++m_rules[anomaly.ruleName()];

    InterfaceStats &stats = ensureInterface(anomaly.interfaceName());
    stats.totalAnomalies += 1.0;

    emit totalAnomaliesChanged(m_totalAnomalies);
}

void StatisticsCollector::flushRates()
{
    for (auto it = m_interfaces.begin(); it != m_interfaces.end(); ++it) {
        InterfaceStats &stats = it.value();
        stats.pps = static_cast<double>(m_pendingPackets.value(it.key(), 0));
        stats.bps = static_cast<double>(m_pendingBytes.value(it.key(), 0));
        stats.history.append(stats.pps);
        stats.historyKbps.append(stats.bps * 8.0 / 1000.0);
        while (stats.history.size() > kHistorySamples)
            stats.history.removeFirst();
        while (stats.historyKbps.size() > kHistorySamples)
            stats.historyKbps.removeFirst();
    }
    m_pendingPackets.clear();
    m_pendingBytes.clear();
}

void StatisticsCollector::reset()
{
    m_interfaces.clear();
    m_protocols.clear();
    m_severities.clear();
    m_rules.clear();
    m_pendingPackets.clear();
    m_pendingBytes.clear();
    m_totalPackets = 0;
    m_totalBytes = 0;
    m_totalAnomalies = 0;
    emit statsChanged();
}

QJsonObject StatisticsCollector::summaryJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("totalPackets"), m_totalPackets);
    root.insert(QStringLiteral("totalBytes"), m_totalBytes);
    root.insert(QStringLiteral("totalAnomalies"), m_totalAnomalies);
    root.insert(QStringLiteral("updatedAt"),
                QDateTime::currentDateTime().toString(Qt::ISODate));

    QJsonArray interfaces;
    for (auto it = m_interfaces.constBegin(); it != m_interfaces.constEnd(); ++it) {
        const InterfaceStats &s = it.value();
        QJsonObject o;
        o.insert(QStringLiteral("name"), CaptureDevice::friendlyName(s.name));
        o.insert(QStringLiteral("packets"), s.packets);
        o.insert(QStringLiteral("bytes"), s.bytes);
        o.insert(QStringLiteral("packetsPerSec"), s.pps);
        o.insert(QStringLiteral("anomalies"), static_cast<qint64>(s.totalAnomalies));
        interfaces.append(o);
    }
    root.insert(QStringLiteral("interfaces"), interfaces);

    QJsonObject protocols;
    for (auto it = m_protocols.constBegin(); it != m_protocols.constEnd(); ++it)
        protocols.insert(it.key(), it.value());
    root.insert(QStringLiteral("protocols"), protocols);

    QJsonObject anomaliesByRule;
    for (auto it = m_rules.constBegin(); it != m_rules.constEnd(); ++it)
        anomaliesByRule.insert(it.key(), it.value());
    root.insert(QStringLiteral("anomaliesByRule"), anomaliesByRule);
    return root;
}

QString StatisticsCollector::summaryText() const
{
    QString text;
    text += QStringLiteral("Пакетов всего: %1, байт: %2, аномалий: %3\n")
                .arg(m_totalPackets)
                .arg(m_totalBytes)
                .arg(m_totalAnomalies);

    for (auto it = m_interfaces.constBegin(); it != m_interfaces.constEnd(); ++it) {
        const InterfaceStats &s = it.value();
        text += QStringLiteral("Интерфейс %1: пакетов %2, байт %3, скорость %4 пак/с, аномалий %5\n")
                    .arg(CaptureDevice::friendlyName(s.name))
                    .arg(s.packets)
                    .arg(s.bytes)
                    .arg(s.pps, 0, 'f', 1)
                    .arg(static_cast<qint64>(s.totalAnomalies));
    }

    QStringList protocols;
    for (auto it = m_protocols.constBegin(); it != m_protocols.constEnd(); ++it)
        protocols << QStringLiteral("%1: %2").arg(it.key()).arg(it.value());
    protocols.sort();
    text += QStringLiteral("Протоколы: ") + protocols.join(QLatin1String(", ")) + QLatin1Char('\n');
    return text;
}
