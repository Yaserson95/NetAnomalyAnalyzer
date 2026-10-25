#pragma once

#include "Notifier.h"
#include "../settings/Settings.h"

#include <QNetworkAccessManager>

/**
 * @brief Оповещение во внешнюю систему по HTTP POST (вебхук):
 * произвольный URL, тело — JSON с описанием аномалии.
 */
class WebhookNotifier : public Notifier {
    Q_OBJECT
public:
    explicit WebhookNotifier(QObject *parent = nullptr);

    QString channelName() const override { return QStringLiteral("Вебхук"); }
    bool isEnabled() const override;
    void notify(const Anomaly &anomaly) override;

private:
    QNetworkAccessManager m_network;
};
