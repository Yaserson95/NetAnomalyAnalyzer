#include "TelegramNotifier.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

TelegramNotifier::TelegramNotifier(QObject *parent)
    : Notifier(parent)
{
}

bool TelegramNotifier::isEnabled() const
{
    const NotificationSettings &settings = Settings::instance().notifications();
    return settings.telegramEnabled &&
           !settings.telegramBotToken.trimmed().isEmpty() &&
           !settings.telegramChatId.trimmed().isEmpty();
}

void TelegramNotifier::notify(const Anomaly &anomaly)
{
    if (!isEnabled()) {
        emit failed(channelName(), tr("Telegram не настроен"));
        return;
    }
    sendText(QStringLiteral("<b>%1</b>\n%2\nИсточник: %3 → %4\nИнтерфейс: %5\n%6")
                 .arg(anomaly.severityName().toHtmlEscaped(),
                      anomaly.title().toHtmlEscaped(),
                      anomaly.sourceIp(),
                      anomaly.destinationIp(),
                      anomaly.interfaceName(),
                      anomaly.description().toHtmlEscaped()));
}

void TelegramNotifier::sendText(const QString &text)
{
    const NotificationSettings settings = Settings::instance().notifications();
    if (!isEnabled()) {
        emit failed(channelName(), tr("Telegram не настроен"));
        return;
    }

    QUrl url(QStringLiteral("https://api.telegram.org/bot%1/sendMessage")
                 .arg(settings.telegramBotToken));
    QJsonObject body;
    body.insert(QStringLiteral("chat_id"), settings.telegramChatId);
    body.insert(QStringLiteral("text"), text);
    body.insert(QStringLiteral("parse_mode"), QStringLiteral("HTML"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(15000);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            emit sent(channelName(), tr("доставлено"));
        } else {
            emit failed(channelName(), reply->errorString());
        }
    });
}
