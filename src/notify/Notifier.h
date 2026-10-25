#pragma once

#include "../anomaly/Anomaly.h"

#include <QObject>

/**
 * @brief Абстрактный канал оповещения об аномалии (Strategy).
 */
class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(QObject *parent = nullptr);
    ~Notifier() override;

    /** Краткое имя канала (для статуса и журнала). */
    virtual QString channelName() const = 0;
    /** Включён ли канал в настройках. */
    virtual bool isEnabled() const = 0;
    /**
     * Журнальный канал пишет каждое оповещение в notifications.jsonl
     * всегда — фильтр по статусам на него не действует (журнал полон,
     * фильтруются только каналы оповещений).
     */
    virtual bool isJournalChannel() const { return false; }
    /** Отправить оповещение. Ответ по сигналу sent(). */
    virtual void notify(const Anomaly &anomaly) = 0;

signals:
    void sent(const QString &channel, const QString &status);
    void failed(const QString &channel, const QString &error);
};
