#pragma once

#include "LlmClient.h"
#include "../settings/Settings.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/**
 * @brief Клиент OpenAI-совместимого HTTP API (OpenAI, OpenRouter, Ollama,
 * LM Studio, vLLM и т.п.) — любой провайдер с путём /chat/completions.
 */
class OpenAiCompatibleClient : public LlmClient {
    Q_OBJECT
public:
    explicit OpenAiCompatibleClient(QObject *parent = nullptr);

    void analyze(const QString &systemPrompt, const QString &userPrompt,
                 const LlmSettings &settings) override;
    void cancel() override;

    /** Простая проверка соединения («Тест» в настройках). */
    void testConnection(const LlmSettings &settings) override;

private:
    void post(const LlmSettings &settings, const QJsonArray &messages,
              const QString &purpose);
    void handleReply(QNetworkReply *reply);

    QNetworkAccessManager m_network;
    QNetworkReply        *m_pendingReply = nullptr;
    QElapsedTimer         m_timer;
    QString               m_purpose;
};
