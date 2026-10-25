#pragma once

#include <QObject>

class InterfaceManager;
class AnomalyDetector;
class StatisticsCollector;
class LogWriter;
class NotificationManager;
class LlmClient;
class LlmScheduler;

/**
 * @brief Контроллер приложения: создаёт подсистемы ядра и связывает их
 * потоки данных. Не зависит от GUI — им пользуются и графическая
 * оболочка (MainWindow), и консольный/служебный запуск (service_main).
 *
 * Поток данных:
 *   InterfaceManager → AnomalyDetector → NotificationManager / LogWriter
 *                  └→ StatisticsCollector → StatsWidget (GUI)
 *                  └→ MainWindow (таблица пакетов)
 *   LlmScheduler → LogWriter + StatisticsCollector → LlmClient → LlmJournal
 *
 * GUI-сигналы NotificationManager (guiNotification, alertSound) в
 * консольном режиме остаются неподключёнными — оповещения уходят только
 * по настроенным каналам (журнал, Telegram, вебхук, почта).
 */
class AppController : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    // Геттеры подсистем — для оболочек (GUI или консольной).
    InterfaceManager    *interfaces()  const { return m_interfaces; }
    AnomalyDetector     *detector()    const { return m_detector; }
    StatisticsCollector *statistics()  const { return m_statistics; }
    LogWriter           *logWriter()   const { return m_logWriter; }
    NotificationManager *notifications() const { return m_notifications; }
    LlmScheduler        *llmScheduler() const { return m_llmScheduler; }
    LlmClient           *llmClient()   const { return m_llmClient; }

private:
    void wireSubsystems();

    InterfaceManager    *m_interfaces = nullptr;
    AnomalyDetector     *m_detector = nullptr;
    StatisticsCollector *m_statistics = nullptr;
    LogWriter           *m_logWriter = nullptr;
    NotificationManager *m_notifications = nullptr;
    LlmClient           *m_llmClient = nullptr;
    LlmScheduler        *m_llmScheduler = nullptr;
};
