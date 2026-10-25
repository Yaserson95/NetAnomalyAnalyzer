#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

namespace {
const auto kEnabledInterfaces = "interfaces/enabled";
const auto kDetectionGroup    = "detection";
const auto kLlmGroup          = "llm";
const auto kNotifyGroup       = "notifications";
const auto kLoggingGroup      = "logging";
const auto kCaptureGroup      = "capture";

#ifdef Q_OS_WIN
/** Базовый каталог данных машины: `%ProgramData%\NetAnalyzer`. */
QString machineDataDir()
{
    QString programData = qEnvironmentVariable("ProgramData");
    if (programData.isEmpty())
        programData = QStringLiteral("C:/ProgramData");
    return programData + QStringLiteral("/NetAnalyzer");
}
#endif

/**
 * Однократная миграция настроек из старого хранилища (реестр Windows,
 * HKCU\Software\NetAnalyzer\NetAnomalyAnalyzer) в ini-файл при первом запуске.
 */
void migrateLegacySettings()
{
#ifdef Q_OS_WIN
    // Миграция выполняется только для основного приложения: тесты работают
    // со своими именами и не должны трогать чужие ключи.
    if (QCoreApplication::organizationName() != QLatin1String("NetAnalyzer")
        || QCoreApplication::applicationName() != QLatin1String("NetAnomalyAnalyzer"))
        return;
    if (QFile::exists(Settings::settingsFilePath()))
        return; // файл настроек уже есть — текущие значения не перезаписываем

    const QSettings legacy; // реестр: HKCU\Software\NetAnalyzer\NetAnomalyAnalyzer
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty())
        return;

    QSettings dst = Settings::openSettings();
    for (const QString &key : keys) {
        // «Плоские» ключи старого реестра с обратным слэшем — шум, у которых
        // нет осмысленного соответствия в ini; пропускаем.
        if (key.contains(QLatin1Char('\\')))
            continue;
        // Старое значение по умолчанию «logs» — относительный каталог;
        // вместо него применяется машинный каталог журнала по умолчанию.
        if (key == QLatin1String("logging/dir")
            && legacy.value(key).toString() == QLatin1String("logs"))
            continue;
        dst.setValue(key, legacy.value(key));
    }
    dst.sync();
#else
    // Хранилище на Linux всегда было ini-файл (каталог пользователя) —
    // миграция на машинные каталоги не требуется.
#endif
}
} // namespace

Settings &Settings::instance()
{
    static Settings settings;
    return settings;
}

Settings::Settings()
{
    load();
}

void Settings::reload()
{
    load();
    emit changed();
    emit llmChanged();
}

QString Settings::configDir()
{
    // Переопределение для тестов и переносных установок.
    const QString override = qEnvironmentVariable("NETAN_CONFIG_DIR");
    if (!override.isEmpty())
        return override;
#ifdef Q_OS_WIN
    return machineDataDir();
#elif defined(Q_OS_LINUX)
    return QStringLiteral("/etc/netanalyzer");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/netanalyzer");
#endif
}

QString Settings::settingsFilePath()
{
    QString app = QCoreApplication::applicationName();
    if (app.isEmpty())
        app = QStringLiteral("NetAnomalyAnalyzer");
    return configDir() + QLatin1Char('/') + app + QStringLiteral(".ini");
}

QSettings Settings::openSettings()
{
    // QSettings сам создаёт файл настроек, но не родительские каталоги.
    QDir().mkpath(configDir());
    return QSettings(settingsFilePath(), QSettings::IniFormat);
}

QString Settings::defaultLogDir()
{
#ifdef Q_OS_WIN
    return machineDataDir() + QStringLiteral("/logs");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("/var/log/netanalyzer");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/netanalyzer/logs");
#endif
}

void Settings::load()
{
    migrateLegacySettings();
    QSettings s = openSettings();

    // Интерфейсы
    const QStringList enabled = s.value(QLatin1String(kEnabledInterfaces)).toStringList();
    m_interfaces.enabledNames = QSet<QString>(enabled.begin(), enabled.end());

    // Детектор
    s.beginGroup(QLatin1String(kDetectionGroup));
    m_detection.enablePortScan       = s.value(QStringLiteral("portScan/enabled"), true).toBool();
    m_detection.enableSynFlood       = s.value(QStringLiteral("synFlood/enabled"), true).toBool();
    m_detection.enableUdpFlood       = s.value(QStringLiteral("udpFlood/enabled"), true).toBool();
    m_detection.enableIcmpFlood      = s.value(QStringLiteral("icmpFlood/enabled"), true).toBool();
    m_detection.enableBroadcast      = s.value(QStringLiteral("broadcast/enabled"), true).toBool();
    m_detection.enableSuspiciousPort = s.value(QStringLiteral("suspiciousPort/enabled"), true).toBool();
    m_detection.enablePayload        = s.value(QStringLiteral("payload/enabled"), true).toBool();
    m_detection.portScanPortThreshold = s.value(QStringLiteral("portScan/threshold"),
                                                m_detection.portScanPortThreshold).toInt();
    m_detection.portScanWindowSec     = s.value(QStringLiteral("portScan/windowSec"),
                                                m_detection.portScanWindowSec).toInt();
    m_detection.synFloodPps   = s.value(QStringLiteral("synFlood/pps"), m_detection.synFloodPps).toInt();
    m_detection.udpFloodPps   = s.value(QStringLiteral("udpFlood/pps"), m_detection.udpFloodPps).toInt();
    m_detection.icmpFloodPps  = s.value(QStringLiteral("icmpFlood/pps"), m_detection.icmpFloodPps).toInt();
    m_detection.broadcastPps  = s.value(QStringLiteral("broadcast/pps"), m_detection.broadcastPps).toInt();
    m_detection.anomalyCooldownSec = s.value(QStringLiteral("cooldownSec"),
                                             m_detection.anomalyCooldownSec).toInt();
    m_detection.suspiciousPorts = s.value(QStringLiteral("suspiciousPorts"),
                                          m_detection.suspiciousPorts).toStringList();
    m_detection.payloadSignatures = s.value(QStringLiteral("payloadSignatures"),
                                            m_detection.payloadSignatures).toStringList();
    s.endGroup();

    // LLM
    s.beginGroup(QLatin1String(kLlmGroup));
    m_llm.enabled      = s.value(QStringLiteral("enabled"), false).toBool();
    m_llm.baseUrl      = s.value(QStringLiteral("baseUrl"), m_llm.baseUrl).toString();
    m_llm.apiKey       = s.value(QStringLiteral("apiKey")).toString();
    m_llm.model        = s.value(QStringLiteral("model"), m_llm.model).toString();
    m_llm.intervalMin  = s.value(QStringLiteral("intervalMin"), m_llm.intervalMin).toInt();
    m_llm.maxTokens    = s.value(QStringLiteral("maxTokens"), m_llm.maxTokens).toInt();
    m_llm.temperature  = s.value(QStringLiteral("temperature"), m_llm.temperature).toDouble();
    m_llm.contextLines = s.value(QStringLiteral("contextLines"), m_llm.contextLines).toInt();
    m_llm.contextBudgetTokens = s.value(QStringLiteral("contextBudgetTokens"),
                                         m_llm.contextBudgetTokens).toInt();
    m_llm.systemPrompt = s.value(QStringLiteral("systemPrompt"), m_llm.systemPrompt).toString();
    s.endGroup();

    // Оповещения
    s.beginGroup(QLatin1String(kNotifyGroup));
    m_notifications.guiPopups       = s.value(QStringLiteral("guiPopups"), true).toBool();
    m_notifications.sound           = s.value(QStringLiteral("sound"), false).toBool();
    m_notifications.minimizeToTray  = s.value(QStringLiteral("minimizeToTray"), true).toBool();
    m_notifications.telegramEnabled = s.value(QStringLiteral("telegram/enabled"), false).toBool();
    m_notifications.telegramBotToken= s.value(QStringLiteral("telegram/token")).toString();
    m_notifications.telegramChatId  = s.value(QStringLiteral("telegram/chatId")).toString();
    m_notifications.webhookEnabled  = s.value(QStringLiteral("webhook/enabled"), false).toBool();
    m_notifications.webhookUrl      = s.value(QStringLiteral("webhook/url")).toString();

    m_notifications.severityInfo     = s.value(QStringLiteral("severity/info"), true).toBool();
    m_notifications.severityWarning  = s.value(QStringLiteral("severity/warning"), true).toBool();
    m_notifications.severityCritical = s.value(QStringLiteral("severity/critical"), true).toBool();

    m_notifications.emailEnabled  = s.value(QStringLiteral("email/enabled"), false).toBool();
    m_notifications.emailHost     = s.value(QStringLiteral("email/host")).toString();
    m_notifications.emailPort     = s.value(QStringLiteral("email/port"), 465).toInt();
    m_notifications.emailSecurity = s.value(QStringLiteral("email/security"), 2).toInt();
    m_notifications.emailUsername = s.value(QStringLiteral("email/username")).toString();
    m_notifications.emailPassword = s.value(QStringLiteral("email/password")).toString();
    m_notifications.emailFrom     = s.value(QStringLiteral("email/from")).toString();
    m_notifications.emailTo       = s.value(QStringLiteral("email/to")).toString();
    s.endGroup();

    // Логи
    s.beginGroup(QLatin1String(kLoggingGroup));
    m_logging.logPackets = s.value(QStringLiteral("packets"), false).toBool();
    m_logging.logDir     = s.value(QStringLiteral("dir"), defaultLogDir()).toString();
    m_logging.maxFileMb  = s.value(QStringLiteral("maxFileMb"), m_logging.maxFileMb).toInt();
    s.endGroup();

    // Захват
    s.beginGroup(QLatin1String(kCaptureGroup));
    m_capture.promiscuous      = s.value(QStringLiteral("promiscuous"), true).toBool();
    m_capture.snaplen          = s.value(QStringLiteral("snaplen"), 65535).toInt();
    m_capture.bpfFilter        = s.value(QStringLiteral("bpfFilter")).toString();
    m_capture.autoStart        = s.value(QStringLiteral("autoStart"), false).toBool();
    m_capture.maxTablePackets  = qBound(50, s.value(QStringLiteral("maxTablePackets"),
                                                     300).toInt(), 100000);
    s.endGroup();
}

void Settings::setInterfaces(const InterfaceSettings &value)
{
    m_interfaces = value;
    saveInterfaces();
    emit changed();
}

void Settings::setDetection(const DetectionSettings &value)
{
    m_detection = value;
    saveDetection();
    emit changed();
}

void Settings::setLlm(const LlmSettings &value)
{
    m_llm = value;
    saveLlm();
    emit changed();
    emit llmChanged();
}

void Settings::setNotifications(const NotificationSettings &value)
{
    m_notifications = value;
    saveNotifications();
    emit changed();
}

void Settings::setLogging(const LoggingSettings &value)
{
    m_logging = value;
    saveLogging();
    emit changed();
}

void Settings::setCapture(const CaptureSettings &value)
{
    m_capture = value;
    saveCapture();
    emit changed();
}

void Settings::saveInterfaces() const
{
    QSettings s = openSettings();
    QStringList enabled(m_interfaces.enabledNames.begin(), m_interfaces.enabledNames.end());
    enabled.sort();
    s.setValue(QLatin1String(kEnabledInterfaces), enabled);
}

void Settings::saveDetection() const
{
    QSettings s = openSettings();
    s.beginGroup(QLatin1String(kDetectionGroup));
    s.setValue(QStringLiteral("portScan/enabled"), m_detection.enablePortScan);
    s.setValue(QStringLiteral("synFlood/enabled"), m_detection.enableSynFlood);
    s.setValue(QStringLiteral("udpFlood/enabled"), m_detection.enableUdpFlood);
    s.setValue(QStringLiteral("icmpFlood/enabled"), m_detection.enableIcmpFlood);
    s.setValue(QStringLiteral("broadcast/enabled"), m_detection.enableBroadcast);
    s.setValue(QStringLiteral("suspiciousPort/enabled"), m_detection.enableSuspiciousPort);
    s.setValue(QStringLiteral("payload/enabled"), m_detection.enablePayload);
    s.setValue(QStringLiteral("portScan/threshold"), m_detection.portScanPortThreshold);
    s.setValue(QStringLiteral("portScan/windowSec"), m_detection.portScanWindowSec);
    s.setValue(QStringLiteral("synFlood/pps"), m_detection.synFloodPps);
    s.setValue(QStringLiteral("udpFlood/pps"), m_detection.udpFloodPps);
    s.setValue(QStringLiteral("icmpFlood/pps"), m_detection.icmpFloodPps);
    s.setValue(QStringLiteral("broadcast/pps"), m_detection.broadcastPps);
    s.setValue(QStringLiteral("cooldownSec"), m_detection.anomalyCooldownSec);
    s.setValue(QStringLiteral("suspiciousPorts"), m_detection.suspiciousPorts);
    s.setValue(QStringLiteral("payloadSignatures"), m_detection.payloadSignatures);
    s.endGroup();
}

void Settings::saveLlm() const
{
    QSettings s = openSettings();
    s.beginGroup(QLatin1String(kLlmGroup));
    s.setValue(QStringLiteral("enabled"), m_llm.enabled);
    s.setValue(QStringLiteral("baseUrl"), m_llm.baseUrl);
    s.setValue(QStringLiteral("apiKey"), m_llm.apiKey);
    s.setValue(QStringLiteral("model"), m_llm.model);
    s.setValue(QStringLiteral("intervalMin"), m_llm.intervalMin);
    s.setValue(QStringLiteral("maxTokens"), m_llm.maxTokens);
    s.setValue(QStringLiteral("temperature"), m_llm.temperature);
    s.setValue(QStringLiteral("contextLines"), m_llm.contextLines);
    s.setValue(QStringLiteral("contextBudgetTokens"), m_llm.contextBudgetTokens);
    s.setValue(QStringLiteral("systemPrompt"), m_llm.systemPrompt);
    s.endGroup();
}

void Settings::saveNotifications() const
{
    QSettings s = openSettings();
    s.beginGroup(QLatin1String(kNotifyGroup));
    s.setValue(QStringLiteral("guiPopups"), m_notifications.guiPopups);
    s.setValue(QStringLiteral("sound"), m_notifications.sound);
    s.setValue(QStringLiteral("minimizeToTray"), m_notifications.minimizeToTray);
    s.setValue(QStringLiteral("telegram/enabled"), m_notifications.telegramEnabled);
    s.setValue(QStringLiteral("telegram/token"), m_notifications.telegramBotToken);
    s.setValue(QStringLiteral("telegram/chatId"), m_notifications.telegramChatId);
    s.setValue(QStringLiteral("webhook/enabled"), m_notifications.webhookEnabled);
    s.setValue(QStringLiteral("webhook/url"), m_notifications.webhookUrl);

    s.setValue(QStringLiteral("severity/info"), m_notifications.severityInfo);
    s.setValue(QStringLiteral("severity/warning"), m_notifications.severityWarning);
    s.setValue(QStringLiteral("severity/critical"), m_notifications.severityCritical);

    s.setValue(QStringLiteral("email/enabled"), m_notifications.emailEnabled);
    s.setValue(QStringLiteral("email/host"), m_notifications.emailHost);
    s.setValue(QStringLiteral("email/port"), m_notifications.emailPort);
    s.setValue(QStringLiteral("email/security"), m_notifications.emailSecurity);
    s.setValue(QStringLiteral("email/username"), m_notifications.emailUsername);
    s.setValue(QStringLiteral("email/password"), m_notifications.emailPassword);
    s.setValue(QStringLiteral("email/from"), m_notifications.emailFrom);
    s.setValue(QStringLiteral("email/to"), m_notifications.emailTo);
    s.endGroup();
}

void Settings::saveLogging() const
{
    QSettings s = openSettings();
    s.beginGroup(QLatin1String(kLoggingGroup));
    s.setValue(QStringLiteral("packets"), m_logging.logPackets);
    s.setValue(QStringLiteral("dir"), m_logging.logDir);
    s.setValue(QStringLiteral("maxFileMb"), m_logging.maxFileMb);
    s.endGroup();
}

void Settings::saveCapture() const
{
    QSettings s = openSettings();
    s.beginGroup(QLatin1String(kCaptureGroup));
    s.setValue(QStringLiteral("promiscuous"), m_capture.promiscuous);
    s.setValue(QStringLiteral("snaplen"), m_capture.snaplen);
    s.setValue(QStringLiteral("bpfFilter"), m_capture.bpfFilter);
    s.setValue(QStringLiteral("autoStart"), m_capture.autoStart);
    s.setValue(QStringLiteral("maxTablePackets"), m_capture.maxTablePackets);
    s.endGroup();
}
