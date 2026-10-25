#pragma once

#include <QWidget>

class LlmScheduler;
class QCheckBox;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QLabel;
class QPushButton;

/**
 * @brief Настройки подключения LLM (совместимые с OpenAI API провайдеры:
 * OpenAI, OpenRouter, Ollama, LM Studio и т.д.).
 */
class LlmSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit LlmSettingsWidget(LlmScheduler *scheduler, QWidget *parent = nullptr);

    void load();
    void apply();

private slots:
    void onTestClicked();
    void onSendNowClicked();

private:
    LlmScheduler   *m_scheduler = nullptr;
    QCheckBox      *m_enabledCheck = nullptr;
    QLineEdit      *m_baseUrlEdit = nullptr;
    QLineEdit      *m_apiKeyEdit = nullptr;
    QLineEdit      *m_modelEdit = nullptr;
    QSpinBox       *m_intervalSpin = nullptr;
    QSpinBox       *m_maxTokensSpin = nullptr;
    QDoubleSpinBox *m_temperatureSpin = nullptr;
    QSpinBox       *m_contextLinesSpin = nullptr;
    QSpinBox       *m_budgetTokensSpin = nullptr;
    QPlainTextEdit *m_systemPromptEdit = nullptr;
    QPushButton    *m_testButton = nullptr;
    QPushButton    *m_sendNowButton = nullptr;
    QLabel         *m_statusLabel = nullptr;
};
