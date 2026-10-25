#pragma once

#include "AnomalyRule.h"
#include "RateWindow.h"

/**
 * @brief Детектор SYN-флуда: аномальная плотность SYN-пакетов к одному хосту.
 */
class SynFloodRule : public AnomalyRule {
public:
    explicit SynFloodRule(const DetectionSettings *settings);

    QString name() const override { return QStringLiteral("SYN-флуд"); }
    std::optional<Anomaly> check(const Packet &packet) override;
    void reset() override;

private:
    RateWindow m_window { 1000 };
};
