#pragma once

#include "../anomaly/Anomaly.h"
#include "../core/Packet.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QVector>

/**
 * @brief Агрегатор статистики по интерфейсам и протоколам.
 *
 * Ведёт счётчики пакетов/байт, мгновенные скорости (обновление раз в
 * секунду) и кольцевой буфер истории для графиков загруженности.
 */
class StatisticsCollector : public QObject {
    Q_OBJECT
public:
    static constexpr int kHistorySamples = 300;   ///< ~5 минут при разряде 1 с
    static constexpr int kIntervalMs = 1000;

    struct InterfaceStats {
        QString name;
        qint64  packets = 0;
        qint64  bytes = 0;
        double  pps = 0.0;      ///< пакетов/сек (последняя секунда)
        double  bps = 0.0;      ///< байт/сек
        double  totalAnomalies = 0;
        QVector<double> history;     ///< pps по времени
        QVector<double> historyKbps; ///< кбит/с по времени
    };

    explicit StatisticsCollector(QObject *parent = nullptr);

public slots:
    void onPacket(const Packet &packet);
    void onAnomaly(const Anomaly &anomaly);

    /** Сбросить всю статистику. */
    void reset();

signals:
    /** Используется таймером — публикуем новый срез скоростей. */
    void tick();
    void statsChanged();
    void totalAnomaliesChanged(qint64 total);

public:
    const QHash<QString, InterfaceStats> &interfaceStats() const { return m_interfaces; }
    const QHash<QString, qint64> &protocolCounts() const { return m_protocols; }
    const QHash<QString, qint64> &severityCounts() const { return m_severities; }
    const QHash<QString, qint64> &ruleCounts() const { return m_rules; }

    qint64 totalPackets() const { return m_totalPackets; }
    qint64 totalBytes() const { return m_totalBytes; }
    qint64 totalAnomalies() const { return m_totalAnomalies; }

    /** Сводка в виде JSON (для промпта LLM). */
    QJsonObject summaryJson() const;
    QString summaryText() const;

private:
    void flushRates();
    InterfaceStats &ensureInterface(const QString &name);

    QHash<QString, InterfaceStats> m_interfaces;
    QHash<QString, qint64>         m_protocols;
    QHash<QString, qint64>         m_severities;
    QHash<QString, qint64>         m_rules;
    QHash<QString, qint64>         m_pendingPackets;
    QHash<QString, qint64>         m_pendingBytes;

    qint64 m_totalPackets = 0;
    qint64 m_totalBytes = 0;
    qint64 m_totalAnomalies = 0;
};
