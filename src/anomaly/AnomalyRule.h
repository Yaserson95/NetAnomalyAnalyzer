#pragma once

#include "Anomaly.h"
#include "../core/Packet.h"
#include "../settings/Settings.h"

#include <memory>
#include <optional>
#include <vector>

/**
 * @brief Базовый интерфейс правила обнаружения аномалий (Strategy).
 *
 * Каждое правило инкапсулирует собственную эвристику и внутреннее
 * состояние (окна, счётчики). Новые правила добавляются наследованием.
 */
class AnomalyRule {
public:
    explicit AnomalyRule(const DetectionSettings *settings);
    virtual ~AnomalyRule() = default;

    /** Человекочитаемое имя правила (в журнале и оповещениях). */
    virtual QString name() const = 0;

    /**
     * @brief Проверить пакет.
     * @return аномалию, если правило сработало, иначе std::nullopt.
     */
    virtual std::optional<Anomaly> check(const Packet &packet) = 0;

    /** Сбросить внутреннее состояние (при остановке захвата). */
    virtual void reset() = 0;

protected:
    Anomaly makeAnomaly(const Packet &packet, Anomaly::Severity severity,
                        const QString &title, const QString &description) const;

    const DetectionSettings &settings() const { return *m_settings; }

private:
    const DetectionSettings *m_settings;
};

/** Фабрика набора стандартных правил. */
std::vector<std::unique_ptr<AnomalyRule>> createDefaultRules(const DetectionSettings *settings);
