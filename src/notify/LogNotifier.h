#pragma once

#include "Notifier.h"

class LogWriter;

/**
 * @brief Оповещение путём записи в notifications.jsonl (всегда доступно).
 */
class LogNotifier : public Notifier {
    Q_OBJECT
public:
    explicit LogNotifier(LogWriter *logWriter, QObject *parent = nullptr);

    QString channelName() const override { return QStringLiteral("Файл журнала"); }
    bool isEnabled() const override { return true; }
    bool isJournalChannel() const override { return true; }
    void notify(const Anomaly &anomaly) override;

private:
    LogWriter *m_logWriter;
};
