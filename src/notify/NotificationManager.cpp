#include "NotificationManager.h"
#include "EmailNotifier.h"
#include "LogNotifier.h"
#include "TelegramNotifier.h"
#include "WebhookNotifier.h"
#include "../logging/LogWriter.h"
#include "../settings/Settings.h"

#include <utility>

NotificationManager::NotificationManager(LogWriter *logWriter, QObject *parent)
    : QObject(parent)
    , m_logWriter(logWriter)
{
    auto registerNotifier = [this](Notifier *notifier) {
        m_notifiers.append(notifier);
        connect(notifier, &Notifier::sent, this,
                [this](const QString &channel, const QString &status) {
                    emit notificationStatus(channel, status);
                });
        connect(notifier, &Notifier::failed, this,
                [this](const QString &channel, const QString &error) {
                    if (m_logWriter)
                        m_logWriter->logError(QStringLiteral("notify/") + channel, error);
                    emit notificationStatus(channel, error);
                });
    };

    registerNotifier(new LogNotifier(m_logWriter, this));
    registerNotifier(new TelegramNotifier(this));
    registerNotifier(new WebhookNotifier(this));
    registerNotifier(new EmailNotifier(this));
}

void NotificationManager::onAnomaly(const Anomaly &anomaly)
{
    const NotificationSettings &settings = Settings::instance().notifications();

    // Фильтр по статусам: отправляются только выбранные
    // уровни — на все каналы, включая сообщения от LLM (они приходят сюда
    // как «Анализ LLM»). Журнальный канал пишет запись всегда.
    const bool allowed = settings.severityAllowed(anomaly.severity());

    for (Notifier *notifier : std::as_const(m_notifiers)) {
        if (!notifier->isEnabled())
            continue;
        if (!allowed && !notifier->isJournalChannel())
            continue;
        notifier->notify(anomaly);
    }

    if (allowed) {
        if (settings.sound)
            emit alertSound();
        emit guiNotification(anomaly);
    }
}

void NotificationManager::sendTestMessage()
{
    Anomaly test;
    test.setId(-1);
    test.setTimestamp(QDateTime::currentDateTime());
    test.setRuleName(tr("Тест"));
    test.setSeverity(Anomaly::Severity::Info);
    test.setTitle(tr("Тестовое оповещение"));
    test.setDescription(tr("Это тест. Система оповещений о сетевых аномалиях работает."));
    test.setSourceIp(QStringLiteral("127.0.0.1"));

    for (Notifier *notifier : std::as_const(m_notifiers)) {
        if (notifier->isEnabled())
            notifier->notify(test);
    }
}
