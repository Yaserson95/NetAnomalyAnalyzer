#pragma once

#include "Notifier.h"
#include "../settings/Settings.h"

#include <QNetworkAccessManager>

/**
 * @brief Оповещение в Telegram через Bot API (sendMessage).
 */
class TelegramNotifier : public Notifier {
    Q_OBJECT
public:
    explicit TelegramNotifier(QObject *parent = nullptr);

    QString channelName() const override { return QStringLiteral("Telegram"); }
    bool isEnabled() const override;
    void notify(const Anomaly &anomaly) override;

    /** Отправить произвольное текстовое сообщение (для теста из настроек). */
    void sendText(const QString &text);

private:
    QNetworkAccessManager m_network;
};
