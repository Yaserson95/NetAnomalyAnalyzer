#pragma once

#include <QObject>
#include <QString>

struct LlmSettings;

/**
 * @brief Абстрактный клиент LLM (интерфейс для подключения разных провайдеров).
 */
class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent = nullptr);
    ~LlmClient() override;

    /** Отправить запрос на анализ. Результат придёт по сигналам. */
    virtual void analyze(const QString &systemPrompt, const QString &userPrompt,
                         const LlmSettings &settings) = 0;
    /** Простая проверка соединения (по умолчанию — ничего). */
    virtual void testConnection(const LlmSettings &settings);
    virtual void cancel();

    bool isBusy() const { return m_busy; }

signals:
    void responseReceived(const QString &response, qint64 elapsedMs);
    void requestFailed(const QString &error);

protected:
    bool m_busy = false;
};
