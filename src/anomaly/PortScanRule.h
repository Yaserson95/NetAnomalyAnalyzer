#pragma once

#include "AnomalyRule.h"

#include <QHash>
#include <QSet>

/**
 * @brief Детектор сканирования портов: один источник трогает
 * много портов одной цели за скользящее окно.
 */
class PortScanRule : public AnomalyRule {
public:
    explicit PortScanRule(const DetectionSettings *settings);

    QString name() const override { return QStringLiteral("Сканирование портов"); }
    std::optional<Anomaly> check(const Packet &packet) override;
    void reset() override;

private:
    struct Target {
        QHash<quint16, qint64> ports; ///< порт -> время последнего пакета
    };
    QHash<QString, QHash<QString, Target>> m_sources; ///< srcIp -> dstIp -> Target
};
