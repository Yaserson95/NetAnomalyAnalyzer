#pragma once

#include "AnomalyRule.h"
#include "RateWindow.h"

/**
 * @brief Детектор шторма широковещательного/мультикастового трафика.
 */
class BroadcastStormRule : public AnomalyRule {
public:
    explicit BroadcastStormRule(const DetectionSettings *settings);

    QString name() const override { return QStringLiteral("Шторм broadcast-трафика"); }
    std::optional<Anomaly> check(const Packet &packet) override;
    void reset() override;

private:
    RateWindow m_window { 1000 };
};
