#include "WebhookNotifier.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

WebhookNotifier::WebhookNotifier(QObject *parent)
    : Notifier(parent)
{
}

bool WebhookNotifier::isEnabled() const
{
    const NotificationSettings &settings = Settings::instance().notifications();
    return settings.webhookEnabled && !settings.webhookUrl.trimmed().isEmpty();
}

void WebhookNotifier::notify(const Anomaly &anomaly)
{
    if (!isEnabled()) {
        emit failed(channelName(), tr("вебхук отключён"));
        return;
    }

    const QJsonObject payload = anomaly.toJson();
    QNetworkRequest request(QUrl(Settings::instance().notifications().webhookUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(15000);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit sent(channelName(), tr("HTTP %1").arg(reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt()));
        } else {
            emit failed(channelName(), reply->errorString());
        }
    });
}
