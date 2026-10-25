#include "NotificationSettingsWidget.h"
#include "../notify/EmailNotifier.h"
#include "../notify/TelegramNotifier.h"
#include "../notify/WebhookNotifier.h"
#include "../settings/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

NotificationSettingsWidget::NotificationSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *guiGroup = new QGroupBox(tr("В интерфейсе"), this);
    auto *guiForm = new QFormLayout(guiGroup);
    m_guiPopupCheck = new QCheckBox(tr("Показывать всплывающие уведомления (трей)"), guiGroup);
    guiForm->addRow(QString(), m_guiPopupCheck);
    m_soundCheck = new QCheckBox(tr("Звуковой сигнал"), guiGroup);
    guiForm->addRow(QString(), m_soundCheck);
    m_trayCheck = new QCheckBox(
        tr("Сворачивать окно в трей при закрытии (работа в фоне)"), guiGroup);
    m_trayCheck->setToolTip(
        tr("При закрытии окна приложение не завершается, а продолжает работать "
           "в трее. Полный выход — пункт «Выход» в меню иконки в трее."));
    guiForm->addRow(QString(), m_trayCheck);
    layout->addWidget(guiGroup);

    // Фильтр по статусам: отправляются только выбранные уровни.
    auto *sevGroup = new QGroupBox(tr("Уровни сообщений (статусы)"), this);
    auto *sevForm = new QFormLayout(sevGroup);
    m_severityInfoCheck = new QCheckBox(tr("Информация"), sevGroup);
    m_severityInfoCheck->setToolTip(
        tr("Отправлять сообщения уровня «Информация» (информационные события)."));
    sevForm->addRow(QString(), m_severityInfoCheck);
    m_severityWarningCheck = new QCheckBox(tr("Предупреждение"), sevGroup);
    m_severityWarningCheck->setToolTip(
        tr("Отправлять сообщения уровня «Предупреждение», включая "
           "вердикты LLM со статусом warning."));
    sevForm->addRow(QString(), m_severityWarningCheck);
    m_severityCriticalCheck = new QCheckBox(tr("Критично"), sevGroup);
    m_severityCriticalCheck->setToolTip(
        tr("Отправлять критические сообщения, включая вердикты LLM "
           "со статусом critical."));
    sevForm->addRow(QString(), m_severityCriticalCheck);
    sevForm->addRow(QString(), new QLabel(
        tr("Фильтр действует на все каналы (Telegram, вебхук, почта, "
           "всплывашки, звук) и на сообщения от LLM. Журнал "
           "notifications.jsonl записывается всегда."), sevGroup));
    layout->addWidget(sevGroup);

    auto *tgGroup = new QGroupBox(tr("Telegram-бот"), this);
    auto *tgForm = new QFormLayout(tgGroup);
    m_telegramCheck = new QCheckBox(tr("Отправлять сообщения в Telegram"), tgGroup);
    tgForm->addRow(QString(), m_telegramCheck);
    m_telegramTokenEdit = new QLineEdit(tgGroup);
    m_telegramTokenEdit->setEchoMode(QLineEdit::Password);
    m_telegramTokenEdit->setPlaceholderText(tr("токен от @BotFather"));
    tgForm->addRow(tr("Токен бота:"), m_telegramTokenEdit);
    m_telegramChatEdit = new QLineEdit(tgGroup);
    m_telegramChatEdit->setPlaceholderText(tr("идентификатор чата, напр. 123456789 или -100…"));
    tgForm->addRow(tr("ID чата:"), m_telegramChatEdit);
    auto *tgTestRow = new QHBoxLayout;
    auto *tgTest = new QPushButton(tr("Отправить тест"), tgGroup);
    tgTestRow->addWidget(tgTest);
    tgTestRow->addStretch();
    tgForm->addRow(QString(), tgTestRow);
    layout->addWidget(tgGroup);

    auto *hookGroup = new QGroupBox(tr("Вебхук (HTTP POST)"), this);
    auto *hookForm = new QFormLayout(hookGroup);
    m_webhookCheck = new QCheckBox(tr("Отправлять POST с JSON об аномалии"), hookGroup);
    hookForm->addRow(QString(), m_webhookCheck);
    m_webhookUrlEdit = new QLineEdit(hookGroup);
    m_webhookUrlEdit->setPlaceholderText(tr("https://example.com/webhook"));
    hookForm->addRow(tr("URL:"), m_webhookUrlEdit);
    auto *hookTestRow = new QHBoxLayout;
    auto *hookTest = new QPushButton(tr("Отправить тест"), hookGroup);
    hookTestRow->addWidget(hookTest);
    hookTestRow->addStretch();
    hookForm->addRow(QString(), hookTestRow);
    layout->addWidget(hookGroup);

    auto *mailGroup = new QGroupBox(tr("Электронная почта (SMTP)"), this);
    auto *mailForm = new QFormLayout(mailGroup);
    m_emailCheck = new QCheckBox(tr("Отправлять оповещения на электронную почту"),
                                 mailGroup);
    mailForm->addRow(QString(), m_emailCheck);
    m_emailHostEdit = new QLineEdit(mailGroup);
    m_emailHostEdit->setPlaceholderText(tr("smtp.example.com"));
    mailForm->addRow(tr("SMTP-сервер:"), m_emailHostEdit);
    m_emailPortSpin = new QSpinBox(mailGroup);
    m_emailPortSpin->setRange(1, 65535);
    m_emailPortSpin->setValue(465);
    mailForm->addRow(tr("Порт:"), m_emailPortSpin);
    m_emailSecurityCombo = new QComboBox(mailGroup);
    m_emailSecurityCombo->addItem(tr("SSL/TLS (обычно 465)"));
    m_emailSecurityCombo->addItem(tr("STARTTLS (обычно 587)"));
    m_emailSecurityCombo->addItem(tr("Без шифрования (обычно 25)"));
    mailForm->addRow(tr("Шифрование:"), m_emailSecurityCombo);
    m_emailUserEdit = new QLineEdit(mailGroup);
    m_emailUserEdit->setPlaceholderText(tr("логин (пусто, если не нужен)"));
    mailForm->addRow(tr("Логин:"), m_emailUserEdit);
    m_emailPasswordEdit = new QLineEdit(mailGroup);
    m_emailPasswordEdit->setEchoMode(QLineEdit::Password);
    mailForm->addRow(tr("Пароль:"), m_emailPasswordEdit);
    m_emailFromEdit = new QLineEdit(mailGroup);
    m_emailFromEdit->setPlaceholderText(tr("netanalyzer@example.com"));
    mailForm->addRow(tr("От кого:"), m_emailFromEdit);
    m_emailToEdit = new QLineEdit(mailGroup);
    m_emailToEdit->setPlaceholderText(tr("admin@example.com, security@example.com"));
    mailForm->addRow(tr("Кому (через запятую):"), m_emailToEdit);
    auto *mailTestRow = new QHBoxLayout;
    auto *mailTest = new QPushButton(tr("Отправить тест"), mailGroup);
    mailTestRow->addWidget(mailTest);
    mailTestRow->addStretch();
    mailForm->addRow(QString(), mailTestRow);
    layout->addWidget(mailGroup);

    layout->addWidget(new QLabel(tr("Журнальный канал (notifications.jsonl) работает всегда."), this));
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);
    layout->addStretch();

    connect(tgTest, &QPushButton::clicked, this, &NotificationSettingsWidget::onTestTelegramClicked);
    connect(hookTest, &QPushButton::clicked, this, &NotificationSettingsWidget::onTestWebhookClicked);
    connect(mailTest, &QPushButton::clicked, this, &NotificationSettingsWidget::onTestEmailClicked);

    load();
}

void NotificationSettingsWidget::load()
{
    const NotificationSettings &n = Settings::instance().notifications();
    m_guiPopupCheck->setChecked(n.guiPopups);
    m_soundCheck->setChecked(n.sound);
    m_trayCheck->setChecked(n.minimizeToTray);
    m_telegramCheck->setChecked(n.telegramEnabled);
    m_telegramTokenEdit->setText(n.telegramBotToken);
    m_telegramChatEdit->setText(n.telegramChatId);
    m_webhookCheck->setChecked(n.webhookEnabled);
    m_webhookUrlEdit->setText(n.webhookUrl);
    m_severityInfoCheck->setChecked(n.severityInfo);
    m_severityWarningCheck->setChecked(n.severityWarning);
    m_severityCriticalCheck->setChecked(n.severityCritical);
    m_emailCheck->setChecked(n.emailEnabled);
    m_emailHostEdit->setText(n.emailHost);
    m_emailPortSpin->setValue(n.emailPort);
    m_emailSecurityCombo->setCurrentIndex(n.emailSecurity);
    m_emailUserEdit->setText(n.emailUsername);
    m_emailPasswordEdit->setText(n.emailPassword);
    m_emailFromEdit->setText(n.emailFrom);
    m_emailToEdit->setText(n.emailTo);
}

void NotificationSettingsWidget::apply()
{
    NotificationSettings n = Settings::instance().notifications();
    n.guiPopups = m_guiPopupCheck->isChecked();
    n.sound = m_soundCheck->isChecked();
    n.minimizeToTray = m_trayCheck->isChecked();
    n.telegramEnabled = m_telegramCheck->isChecked();
    n.telegramBotToken = m_telegramTokenEdit->text().trimmed();
    n.telegramChatId = m_telegramChatEdit->text().trimmed();
    n.webhookEnabled = m_webhookCheck->isChecked();
    n.webhookUrl = m_webhookUrlEdit->text().trimmed();
    n.severityInfo = m_severityInfoCheck->isChecked();
    n.severityWarning = m_severityWarningCheck->isChecked();
    n.severityCritical = m_severityCriticalCheck->isChecked();
    n.emailEnabled = m_emailCheck->isChecked();
    n.emailHost = m_emailHostEdit->text().trimmed();
    n.emailPort = m_emailPortSpin->value();
    n.emailSecurity = m_emailSecurityCombo->currentIndex();
    n.emailUsername = m_emailUserEdit->text().trimmed();
    n.emailPassword = m_emailPasswordEdit->text();
    n.emailFrom = m_emailFromEdit->text().trimmed();
    n.emailTo = m_emailToEdit->text().trimmed();
    Settings::instance().setNotifications(n);
}

void NotificationSettingsWidget::onTestTelegramClicked()
{
    apply();
    auto *notifier = new TelegramNotifier(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#666"));
    m_statusLabel->setText(tr("Отправка тестового сообщения в Telegram…"));
    connect(notifier, &TelegramNotifier::sent, this,
            [this, notifier](const QString &, const QString &status) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#277d38"));
                m_statusLabel->setText(tr("Telegram: тест %1").arg(status));
                notifier->deleteLater();
            });
    connect(notifier, &TelegramNotifier::failed, this,
            [this, notifier](const QString &, const QString &error) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#c0392b"));
                m_statusLabel->setText(tr("Telegram: %1").arg(error));
                notifier->deleteLater();
            });
    notifier->sendText(tr("Тест NetAnomalyAnalyzer: оповещения Telegram настроены верно."));
}

void NotificationSettingsWidget::onTestWebhookClicked()
{
    apply();
    auto *notifier = new WebhookNotifier(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#666"));
    m_statusLabel->setText(tr("Отправка тестового POST…"));
    Anomaly test;
    test.setId(-2);
    test.setTimestamp(QDateTime::currentDateTime());
    test.setRuleName(tr("Тест"));
    test.setSeverity(Anomaly::Severity::Info);
    test.setTitle(tr("Тестовое оповещение вебхука"));
    test.setDescription(tr("Проверка доставки POST-запроса."));
    test.setSourceIp(QStringLiteral("127.0.0.1"));

    connect(notifier, &WebhookNotifier::sent, this,
            [this, notifier](const QString &, const QString &status) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#277d38"));
                m_statusLabel->setText(tr("Вебхук: %1").arg(status));
                notifier->deleteLater();
            });
    connect(notifier, &WebhookNotifier::failed, this,
            [this, notifier](const QString &, const QString &error) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#c0392b"));
                m_statusLabel->setText(tr("Вебхук: %1").arg(error));
                notifier->deleteLater();
            });
    notifier->notify(test);
}

void NotificationSettingsWidget::onTestEmailClicked()
{
    apply();
    auto *notifier = new EmailNotifier(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#666"));
    m_statusLabel->setText(tr("Отправка тестового письма…"));
    connect(notifier, &EmailNotifier::sent, this,
            [this, notifier](const QString &, const QString &status) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#277d38"));
                m_statusLabel->setText(tr("Почта: тест %1").arg(status));
                notifier->deleteLater();
            });
    connect(notifier, &EmailNotifier::failed, this,
            [this, notifier](const QString &, const QString &error) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#c0392b"));
                m_statusLabel->setText(tr("Почта: %1").arg(error));
                notifier->deleteLater();
            });
    notifier->sendText(
        tr("[NetAnomalyAnalyzer] Тест оповещений по электронной почте"),
        tr("Это тестовое письмо. Система оповещений NetAnomalyAnalyzer "
           "настроена верно: письма по SMTP будут доставляться."));
}
