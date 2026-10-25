#pragma once

#include "AnomalyRule.h"

#include <QHash>

/**
 * @brief Детектор подозрительных сигнатур в полезной нагрузке
 * (команды шелла, обращения к системным файлам, SQL-инъекции).
 */
class PayloadSignatureRule : public AnomalyRule {
public:
    explicit PayloadSignatureRule(const DetectionSettings *settings);

    QString name() const override { return QStringLiteral("Опасная сигнатура в payload"); }
    std::optional<Anomaly> check(const Packet &packet) override;
    void reset() override;

private:
    /// Ключ (src|dst|сигнатура) -> время последнего срабатывания (cooldown).
    QHash<QString, qint64> m_lastEmit;
};
