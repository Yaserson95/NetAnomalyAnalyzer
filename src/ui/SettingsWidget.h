#pragma once

#include <QWidget>

class InterfaceManager;
class LlmScheduler;
class InterfacesSettingsWidget;
class DetectionSettingsWidget;
class LlmSettingsWidget;
class NotificationSettingsWidget;
class LoggingSettingsWidget;

/**
 * @brief Вкладка «Настройки»: интерфейсы, правила, LLM, оповещения, логи.
 * Каждая вкладка применяет свои изменения по кнопке «Применить».
 */
class SettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWidget(InterfaceManager *manager, LlmScheduler *llmScheduler,
                            QWidget *parent = nullptr);

    void load();

public slots:
    void apply();

private:
    InterfacesSettingsWidget   *m_interfaces = nullptr;
    DetectionSettingsWidget    *m_detection = nullptr;
    LlmSettingsWidget          *m_llm = nullptr;
    NotificationSettingsWidget *m_notifications = nullptr;
    LoggingSettingsWidget      *m_logging = nullptr;
};
