#include "LogWriter.h"
#include "../settings/Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

LogWriter::LogWriter(QObject *parent)
    : QObject(parent)
{
    const LoggingSettings logging = Settings::instance().logging();
    m_logDir = logging.logDir;
    m_maxFileMb = logging.maxFileMb;
    m_logPackets = logging.logPackets;
    QDir().mkpath(m_logDir);
    setLogDirectory(m_logDir);
}

void LogWriter::setLogDirectory(const QString &dir)
{
    m_logDir = dir;
    QDir().mkpath(m_logDir);
    m_statusStore.setFilePath(m_logDir + QLatin1String("/anomaly-status.json"));
}

void LogWriter::refresh()
{
    const LoggingSettings logging = Settings::instance().logging();
    m_maxFileMb = logging.maxFileMb;
    m_logPackets = logging.logPackets;
    setLogDirectory(logging.logDir);
}

QString LogWriter::anomalyLogPath() const
{
    return m_logDir + QLatin1String("/anomalies.jsonl");
}

void LogWriter::append(const QString &fileName, const QJsonObject &object)
{
    const QString fullPath = m_logDir + QLatin1Char('/') + fileName;
    rotateIfNeeded(fullPath);

    QFile file(fullPath);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;

    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    file.write(line);
}

void LogWriter::rotateIfNeeded(const QString &fullPath)
{
    QFileInfo info(fullPath);
    if (!info.exists() || info.size() < static_cast<qint64>(m_maxFileMb) * 1024 * 1024)
        return;

    const QString rotated = fullPath + QStringLiteral(".1");
    QFile::remove(rotated);
    QFile::rename(fullPath, rotated);
}

void LogWriter::onAnomaly(const Anomaly &anomaly)
{
    append(QStringLiteral("anomalies.jsonl"), anomaly.toJson());
}

void LogWriter::onPacket(const Packet &packet)
{
    if (!m_logPackets)
        return;

    QJsonObject object;
    object.insert(QStringLiteral("timestamp"), packet.timestamp().toString(Qt::ISODate));
    object.insert(QStringLiteral("interface"), packet.interfaceName());
    object.insert(QStringLiteral("srcMac"), packet.sourceMac());
    object.insert(QStringLiteral("dstMac"), packet.destinationMac());
    object.insert(QStringLiteral("src"), packet.sourceIp());
    object.insert(QStringLiteral("dst"), packet.destinationIp());
    object.insert(QStringLiteral("protocol"), packet.protocolName());
    object.insert(QStringLiteral("srcPort"), packet.sourcePort());
    object.insert(QStringLiteral("dstPort"), packet.destinationPort());
    object.insert(QStringLiteral("tcpFlags"), packet.tcpFlagsString());
    object.insert(QStringLiteral("length"), packet.capturedLength());
    object.insert(QStringLiteral("info"), packet.summary());

    append(QStringLiteral("packets-%1.jsonl")
               .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))),
           object);
}

void LogWriter::logNotification(const QString &channel, const Anomaly &anomaly, const QString &status)
{
    QJsonObject object = anomaly.toJson();
    object.insert(QStringLiteral("channel"), channel);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("sentAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    append(QStringLiteral("notifications.jsonl"), object);
}

void LogWriter::logError(const QString &subsystem, const QString &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    object.insert(QStringLiteral("subsystem"), subsystem);
    object.insert(QStringLiteral("message"), message);
    append(QStringLiteral("errors.jsonl"), object);
}

QVector<QJsonObject> readJsonlTail(const QString &path, int maxLines)
{
    QVector<QJsonObject> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    const QByteArray content = file.readAll();
    const QList<QByteArray> lines = content.split('\n');

    for (int i = lines.size() - 1; i >= 0 && result.size() < maxLines; --i) {
        const QByteArray &line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            result.prepend(doc.object());
    }
    return result;
}

QVector<QJsonObject> LogWriter::tailAnomalies(int maxLines) const
{
    QVector<QJsonObject> records = readJsonlTail(anomalyLogPath(), maxLines);
    // Добавляем операторский статус: resolved — аномалия уже разобрана
    for (QJsonObject &record : records) {
        record.insert(QStringLiteral("status"),
                      m_statusStore.status(AnomalyStatusStore::keyFor(record)));
    }
    return records;
}

QVector<QJsonObject> LogWriter::tailNotifications(int maxLines) const
{
    return readJsonlTail(m_logDir + QLatin1String("/notifications.jsonl"), maxLines);
}

QVector<QJsonObject> LogWriter::eventsSince(qint64 sinceMs, int maxLines) const
{
    const QVector<QJsonObject> all =
        readJsonlTail(m_logDir + QLatin1String("/errors.jsonl"), 100000);
    QVector<QJsonObject> result;
    for (const QJsonObject &record : all) {
        const QString ts = record.value(QStringLiteral("timestamp")).toString();
        const QDateTime dt = QDateTime::fromString(ts, Qt::ISODate);
        if (dt.isValid() && dt.toMSecsSinceEpoch() < sinceMs)
            continue;
        result.append(record);
        if (result.size() >= maxLines)
            break;
    }
    return result;
}

QString LogWriter::anomalyStatus(const QString &key) const
{
    return m_statusStore.status(key);
}

void LogWriter::setAnomalyResolved(const QString &key, bool resolved)
{
    m_statusStore.setResolved(key, resolved);
}
