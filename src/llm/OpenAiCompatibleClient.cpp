#include "OpenAiCompatibleClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

OpenAiCompatibleClient::OpenAiCompatibleClient(QObject *parent)
    : LlmClient(parent)
{
}

void OpenAiCompatibleClient::cancel()
{
    if (m_pendingReply) {
        QNetworkReply *reply = m_pendingReply;
        m_pendingReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    LlmClient::cancel();
}

void OpenAiCompatibleClient::analyze(const QString &systemPrompt, const QString &userPrompt,
                                     const LlmSettings &settings)
{
    if (m_busy)
        return;

    QJsonArray messages;
    if (!systemPrompt.isEmpty()) {
        QJsonObject message;
        message.insert(QStringLiteral("role"), QStringLiteral("system"));
        message.insert(QStringLiteral("content"), systemPrompt);
        messages.append(message);
    }
    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), userPrompt);
    messages.append(userMessage);

    m_purpose = QStringLiteral("analyze");
    post(settings, messages, QStringLiteral("analyze"));
}

void OpenAiCompatibleClient::testConnection(const LlmSettings &settings)
{
    if (m_busy)
        return;
    QJsonArray messages;
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"),
                   QStringLiteral("Ответь одним словом: OK"));
    messages.append(message);

    auto probe = settings;
    probe.maxTokens = 10;
    m_purpose = QStringLiteral("test");
    post(probe, messages, QStringLiteral("test"));
}

void OpenAiCompatibleClient::post(const LlmSettings &settings, const QJsonArray &messages,
                                  const QString &purpose)
{
    Q_UNUSED(purpose)
    if (settings.baseUrl.trimmed().isEmpty()) {
        emit requestFailed(tr("Не задан адрес LLM-сервера (baseUrl)."));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), settings.model);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), settings.temperature);
    body.insert(QStringLiteral("max_tokens"), settings.maxTokens);

    QUrl url(settings.baseUrl.trimmed());
    if (url.path().endsWith(QLatin1Char('/')))
        url.setPath(url.path() + QStringLiteral("chat/completions"));
    else
        url.setPath(url.path() + QStringLiteral("/chat/completions"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!settings.apiKey.isEmpty())
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + settings.apiKey.toUtf8());
    request.setTransferTimeout(120000);

    m_busy = true;
    m_timer.start();

    if (m_pendingReply) {
        QNetworkReply *old = m_pendingReply;
        m_pendingReply = nullptr;      // устаревший запрос — не трогает состояние
        old->abort();
        old->deleteLater();
    }
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_pendingReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handleReply(reply);
    });
}

void OpenAiCompatibleClient::handleReply(QNetworkReply *reply)
{
    if (!reply)
        return;
    reply->deleteLater();

    if (reply != m_pendingReply) {
        // Ответ отменённого запроса — игнорируем.
        return;
    }
    m_pendingReply = nullptr;
    m_busy = false;

    const qint64 elapsed = m_timer.elapsed();

    if (reply->error() != QNetworkReply::NoError) {
        QString detail = reply->errorString();
        const QByteArray payload = reply->readAll();
        if (!payload.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            const QString message = doc.object().value(QStringLiteral("error"))
                                        .toObject().value(QStringLiteral("message")).toString();
            if (!message.isEmpty())
                detail += QLatin1String(" — ") + message;
        }
        emit requestFailed(detail);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonObject root = doc.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    QString content;
    if (!choices.isEmpty())
        content = choices.at(0).toObject()
                      .value(QStringLiteral("message")).toObject()
                      .value(QStringLiteral("content")).toString();

    if (content.isEmpty()) {
        emit requestFailed(tr("Пустой ответ от LLM."));
        return;
    }
    emit responseReceived(content, elapsed);
}
