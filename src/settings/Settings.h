#pragma once

#include "../anomaly/Anomaly.h"

#include <QObject>
#include <QSet>
#include <QSettings>
#include <QStringList>

/** Настройки прослушиваемых интерфейсов. */
struct InterfaceSettings {
    QSet<QString> enabledNames;   ///< включённые устройства
};

/** Настройки правил обнаружения аномалий. */
struct DetectionSettings {
    bool enablePortScan     = true;
    bool enableSynFlood     = true;
    bool enableUdpFlood     = true;
    bool enableIcmpFlood    = true;
    bool enableBroadcast    = true;
    bool enableSuspiciousPort = true;
    bool enablePayload      = true;

    int portScanPortThreshold = 15;   ///< уникальных портов на одну цель
    int portScanWindowSec     = 10;   ///< окно анализа, сек
    int synFloodPps           = 100;  ///< SYN-пакетов/сек на хост
    int udpFloodPps           = 300;
    int icmpFloodPps          = 100;
    int broadcastPps          = 200;
    int anomalyCooldownSec    = 10;   ///< защита от спама одинаковыми аномалиями

    QStringList suspiciousPorts { QStringLiteral("4444"), QStringLiteral("31337"),
                                  QStringLiteral("1337"), QStringLiteral("6667") };
    QStringList payloadSignatures { QStringLiteral("cmd.exe"), QStringLiteral("/bin/sh"),
                                    QStringLiteral("etc/passwd"), QStringLiteral("select * from") };
};

/** Настройки LLM-анализа. */
struct LlmSettings {
    bool    enabled     = false;
    QString baseUrl     = QStringLiteral("https://api.openai.com/v1");
    QString apiKey;
    QString model       = QStringLiteral("gpt-4o-mini");
    int     intervalMin = 30;    ///< период отправки по расписанию, минут (0 — выкл.)
    int     maxTokens   = 800;
    double  temperature = 0.2;
    int     contextLines = 100;  ///< сколько последних записей журнала отправлять
    int     contextBudgetTokens = 6000;  ///< бюджет контекста в токенах: при
                                         ///< превышении запрос делится на чанки
    QString systemPrompt = QStringLiteral(
        "Ты — аналитик сетевой безопасности. Тебе переданы журналы сетевого трафика "
        "и обнаруженных аномалий. Выяви возможные инциденты, объясни причины и дай "
        "краткие рекомендации. Отвечай по-русски, структурированно.");
};

/** Настройки способов оповещения и поведения окна. */
struct NotificationSettings {
    bool guiPopups = true;
    bool sound     = false;
    bool minimizeToTray = true;   ///< сворачивать окно в трей при закрытии

    bool    telegramEnabled = false;
    QString telegramBotToken;
    QString telegramChatId;

    bool    webhookEnabled = false;
    QString webhookUrl;

    // Фильтр по статусам: отправляются только выбранные
    // уровни — для всех каналов, включая сообщения от LLM.
    bool severityInfo     = true;
    bool severityWarning  = true;
    bool severityCritical = true;

    // Электронная почта (SMTP, без внешних зависимостей)
    bool    emailEnabled  = false;
    QString emailHost;            ///< SMTP-сервер, напр. smtp.yandex.ru
    int     emailPort    = 465;
    int     emailSecurity = 2;    ///< 0 — без шифрования, 1 — STARTTLS, 2 — SSL/TLS
    QString emailUsername;
    QString emailPassword;
    QString emailFrom;            ///< адрес отправителя
    QString emailTo;              ///< получатели через запятую

    /** Разрешён ли данный статус для отправки (все каналы кроме журнала). */
    bool severityAllowed(Anomaly::Severity severity) const
    {
        switch (severity) {
        case Anomaly::Severity::Info:     return severityInfo;
        case Anomaly::Severity::Warning:  return severityWarning;
        case Anomaly::Severity::Critical: return severityCritical;
        }
        return true;
    }
};

/** Настройки журналирования. */
struct LoggingSettings {
    bool    logPackets = false;              ///< писать каждый пакет в лог-файл
    QString logDir     = QStringLiteral("logs");
    int     maxFileMb  = 20;                 ///< ротация лог-файлов по размеру
};

/** Параметры захвата. */
struct CaptureSettings {
    bool    promiscuous = true;
    int     snaplen     = 65535;
    QString bpfFilter;
    bool    autoStart   = false;
    int     maxTablePackets = 300;           ///< сколько последних пакетов показывать в таблице
};

/**
 * @brief Единое хранилище настроек приложения (обёртка над QSettings).
 *
 * Singleton: Settings::instance(). Хранение — ini-файл, см. openSettings():
 * Windows — `%ProgramData%\NetAnalyzer\<приложение>.ini`,
 * Linux — `/etc/netanalyzer/<приложение>.ini`.
 */
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();

    /**
     * Каталог конфигурации для текущей ОС:
     * Windows — `%ProgramData%\NetAnalyzer`, Linux — `/etc/netanalyzer`.
     */
    static QString configDir();

    /** Полный путь к файлу настроек (ini). */
    static QString settingsFilePath();

    /**
     * QSettings, привязанный к файлу настроек (ini, см. settingsFilePath()).
     * Каталог создаётся при необходимости. Возвращает объект по значению
     * (C++17 guaranteed elision), копирование QSettings не требуется.
     */
    static QSettings openSettings();

    /**
     * Каталог журналов по умолчанию: Windows — `%ProgramData%\NetAnalyzer\logs`,
     * Linux — `/var/log/netanalyzer`.
     */
    static QString defaultLogDir();

    const InterfaceSettings &interfaces() const { return m_interfaces; }
    void setInterfaces(const InterfaceSettings &value);

    const DetectionSettings &detection() const { return m_detection; }
    void setDetection(const DetectionSettings &value);

    const LlmSettings &llm() const { return m_llm; }
    void setLlm(const LlmSettings &value);

    const NotificationSettings &notifications() const { return m_notifications; }
    void setNotifications(const NotificationSettings &value);

    const LoggingSettings &logging() const { return m_logging; }
    void setLogging(const LoggingSettings &value);

    const CaptureSettings &capture() const { return m_capture; }
    void setCapture(const CaptureSettings &value);

    void reload();

signals:
    void changed();               ///< любая настройка изменилась
    void llmChanged();

private:
    Settings();
    void load();
    void saveInterfaces() const;
    void saveDetection() const;
    void saveLlm() const;
    void saveNotifications() const;
    void saveLogging() const;
    void saveCapture() const;

    InterfaceSettings   m_interfaces;
    DetectionSettings   m_detection;
    LlmSettings         m_llm;
    NotificationSettings m_notifications;
    LoggingSettings     m_logging;
    CaptureSettings     m_capture;
};
