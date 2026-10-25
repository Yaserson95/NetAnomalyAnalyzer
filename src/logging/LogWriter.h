#pragma once

#include "../anomaly/Anomaly.h"
#include "../anomaly/AnomalyStatusStore.h"
#include "../core/Packet.h"

#include <QJsonObject>
#include <QObject>
#include <QVector>

/**
 * @brief Пишет журналы в файлы (JSON Lines) с ротацией по размеру.
 *
 * Файлы:
 *  - anomalies.jsonl — обнаруженные аномалии (основной контекст для LLM)
 *  - packets-ГГГГ-ММ-ДД.jsonl — поток пакетов (если включено в настройках)
 *  - notifications.jsonl — отправленные оповещения
 *  - errors.jsonl — журнал событий подсистем (ошибки и события)
 *  - anomaly-status.json — отметки оператора «решено» по аномалиям
 */
class LogWriter : public QObject {
    Q_OBJECT
public:
    explicit LogWriter(QObject *parent = nullptr);

    /** Сменить каталог логов (создаётся при необходимости). */
    void setLogDirectory(const QString &dir);
    QString logDirectory() const { return m_logDir; }

    /** Последние JSON-записи журнала аномалий (для контекста LLM).
     *  В каждую запись добавляется поле "status": detected/resolved. */
    QVector<QJsonObject> tailAnomalies(int maxLines) const;
    /** Последние JSON-записи журнала оповещений. */
    QVector<QJsonObject> tailNotifications(int maxLines) const;
    /** Записи журнала событий (errors.jsonl) начиная с sinceMs. */
    QVector<QJsonObject> eventsSince(qint64 sinceMs, int maxLines) const;

    /** Отметки «решено» по аномалиям (хранилище anomaly-status.json). */
    QString anomalyStatus(const QString &key) const;
    void setAnomalyResolved(const QString &key, bool resolved);

    QString anomalyLogPath() const;

public slots:
    void onAnomaly(const Anomaly &anomaly);
    void onPacket(const Packet &packet);
    void logNotification(const QString &channel, const Anomaly &anomaly, const QString &status);
    void logError(const QString &subsystem, const QString &message);
    /** Сброс учёта размера (при смене настроек). */
    void refresh();

private:
    void append(const QString &fileName, const QJsonObject &object);
    void rotateIfNeeded(const QString &fullPath);

    QString m_logDir;
    int     m_maxFileMb = 20;
    bool    m_logPackets = false;
    AnomalyStatusStore m_statusStore;   ///< отметки «решено» по аномалиям
};

/** Прочитать последние maxLines строк JSONL-файла (с конца). */
QVector<QJsonObject> readJsonlTail(const QString &path, int maxLines);
