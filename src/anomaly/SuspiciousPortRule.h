#pragma once

#include "AnomalyRule.h"

#include <QSet>

/**
 * @brief Детектор трафика на типичных портах бэкдоров/шеллов.
 */
class SuspiciousPortRule : public AnomalyRule {
public:
    explicit SuspiciousPortRule(const DetectionSettings *settings);

    QString name() const override { return QStringLiteral("Подозрительный порт"); }
    std::optional<Anomaly> check(const Packet &packet) override;
    void reset() override;

private:
    QSet<quint16> cachedPorts() const;

    QSet<quint16> m_ports;
    QString       m_lastKey;
    qint64        m_lastEmitMs = 0;
};
