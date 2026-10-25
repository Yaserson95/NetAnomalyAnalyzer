#include "AppController.h"

#include "../anomaly/AnomalyDetector.h"
#include "../core/InterfaceManager.h"
#include "../llm/LlmScheduler.h"
#include "../llm/OpenAiCompatibleClient.h"
#include "../logging/LogWriter.h"
#include "../notify/NotificationManager.h"
#include "../settings/Settings.h"
#include "../stats/StatisticsCollector.h"

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    // Регистрация типов для queued-соединений между потоками.
    qRegisterMetaType<Packet>("Packet");
    qRegisterMetaType<Anomaly>("Anomaly");

    const DetectionSettings &detection = Settings::instance().detection();

    m_logWriter = new LogWriter(this);
    m_interfaces = new InterfaceManager(this);
    m_detector = new AnomalyDetector(&detection, this);
    m_statistics = new StatisticsCollector(this);
    m_notifications = new NotificationManager(m_logWriter, this);
    m_llmClient = new OpenAiCompatibleClient(this);
    m_llmScheduler = new LlmScheduler(m_llmClient, m_statistics, m_logWriter, this);

    wireSubsystems();
}

AppController::~AppController() = default;

void AppController::wireSubsystems()
{
    // Поток пакетов: захват → потребители
    connect(m_interfaces, &InterfaceManager::packetCaptured,
            m_detector, &AnomalyDetector::onPacket, Qt::QueuedConnection);
    connect(m_interfaces, &InterfaceManager::packetCaptured,
            m_statistics, &StatisticsCollector::onPacket, Qt::QueuedConnection);
    connect(m_interfaces, &InterfaceManager::packetCaptured,
            m_logWriter, &LogWriter::onPacket, Qt::QueuedConnection);

    // Аномалии → журнал, статистика, оповещения
    connect(m_detector, &AnomalyDetector::anomalyDetected,
            m_logWriter, &LogWriter::onAnomaly, Qt::QueuedConnection);
    connect(m_detector, &AnomalyDetector::anomalyDetected,
            m_statistics, &StatisticsCollector::onAnomaly, Qt::QueuedConnection);
    connect(m_detector, &AnomalyDetector::anomalyDetected,
            m_notifications, &NotificationManager::onAnomaly, Qt::QueuedConnection);

    // Изменение настроек журналирования
    connect(&Settings::instance(), &Settings::changed, m_logWriter, &LogWriter::refresh);

    // Вердикт LLM уровня warning/critical → те же каналы, что и обычные
    // аномалии: журнал anomalies.jsonl (а значит — контекст следующих
    // запросов с дедупликацией) и оповещения (трей, Telegram, вебхук,
    // почта); список аномалий GUI подключается в MainWindow.
    connect(m_llmScheduler, &LlmScheduler::verdictAnomaly,
            m_logWriter, &LogWriter::onAnomaly);
    connect(m_llmScheduler, &LlmScheduler::verdictAnomaly,
            m_notifications, &NotificationManager::onAnomaly);
}
