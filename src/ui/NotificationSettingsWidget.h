#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;

/**
 * @brief Настройки способов оповещения: всплывающие окна, звук,
 * Telegram-бот, произвольный вебхук, электронная почта (SMTP),
 * а также фильтр по статусам — отправляются только
 * выбранные уровни сообщений, в том числе от LLM.
 */
class NotificationSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit NotificationSettingsWidget(QWidget *parent = nullptr);

    void load();
    void apply();

private slots:
    void onTestTelegramClicked();
    void onTestWebhookClicked();
    void onTestEmailClicked();

private:
    QCheckBox *m_guiPopupCheck = nullptr;
    QCheckBox *m_soundCheck = nullptr;
    QCheckBox *m_trayCheck = nullptr;

    QCheckBox *m_severityInfoCheck = nullptr;
    QCheckBox *m_severityWarningCheck = nullptr;
    QCheckBox *m_severityCriticalCheck = nullptr;

    QCheckBox *m_telegramCheck = nullptr;
    QLineEdit *m_telegramTokenEdit = nullptr;
    QLineEdit *m_telegramChatEdit = nullptr;
    QCheckBox *m_webhookCheck = nullptr;
    QLineEdit *m_webhookUrlEdit = nullptr;

    QCheckBox *m_emailCheck = nullptr;
    QLineEdit *m_emailHostEdit = nullptr;
    QSpinBox  *m_emailPortSpin = nullptr;
    QComboBox *m_emailSecurityCombo = nullptr;
    QLineEdit *m_emailUserEdit = nullptr;
    QLineEdit *m_emailPasswordEdit = nullptr;
    QLineEdit *m_emailFromEdit = nullptr;
    QLineEdit *m_emailToEdit = nullptr;

    QLabel    *m_statusLabel = nullptr;
};
