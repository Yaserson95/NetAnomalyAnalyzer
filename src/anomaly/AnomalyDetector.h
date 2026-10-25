#pragma once

#include "Anomaly.h"
#include "AnomalyRule.h"
#include "../core/Packet.h"

#include <QHash>
#include <QObject>

#include <memory>
#include <vector>

/**
 * @brief Детектор аномалий: прогоняет каждый пакет по набору правил
 * (Chain of Responsibility) и публикует сработавшие аномалии.
 *
 * Внутри дедупликация: одинаковые аномалии (правило + пара хостов)
 * не чаще detection.anomalyCooldownSec.
 */
class AnomalyDetector : public QObject {
    Q_OBJECT
public:
    explicit AnomalyDetector(const DetectionSettings *settings, QObject *parent = nullptr);

public slots:
    void onPacket(const Packet &packet);
    /** Сброс состояния всех правил (например, при остановке захвата). */
    void resetRules();

signals:
    void anomalyDetected(const Anomaly &anomaly);
    void totalAnomaliesChanged(qint64 total);
    void rulesChanged();

private:
    bool isSuppressed(const Anomaly &anomaly, qint64 nowMs);

    const DetectionSettings *m_settings;
    std::vector<std::unique_ptr<AnomalyRule>> m_rules;
    QHash<QString, qint64> m_lastEmitted; ///< "правило|src|dst" -> момент
    qint64 m_total = 0;
    qint64 m_nextId = 1;
};
