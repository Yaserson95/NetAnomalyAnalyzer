#pragma once

#include "AnomalyRule.h"
#include "RateWindow.h"

/**
 * @brief Детектор ICMP-флуда (ping-флуд / smurf) по источнику.
 */
class IcmpFloodRule : public AnomalyRule {
public:
    explicit IcmpFloodRule(const DetectionSettings *settings);

    QString name() const override { return QStringLiteral("ICMP-флуд"); }
    std::optional<Anomaly> check(const Packet &packet) override;
    void reset() override;

private:
    RateWindow m_window { 1000 };
};
