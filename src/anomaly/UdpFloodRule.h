#pragma once

#include "AnomalyRule.h"
#include "RateWindow.h"

/**
 * @brief Детектор UDP-флуда: аномальная плотность UDP-пакетов к одному хосту.
 */
class UdpFloodRule : public AnomalyRule {
public:
    explicit UdpFloodRule(const DetectionSettings *settings);

    QString name() const override { return QStringLiteral("UDP-флуд"); }
    std::optional<Anomaly> check(const Packet &packet) override;
    void reset() override;

private:
    RateWindow m_window { 1000 };
};
