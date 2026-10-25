#include "AnomalyStatusStore.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

const char *AnomalyStatusStore::kDetected = "detected";
const char *AnomalyStatusStore::kResolved = "resolved";

AnomalyStatusStore::AnomalyStatusStore(const QString &filePath)
{
    setFilePath(filePath);
}

void AnomalyStatusStore::setFilePath(const QString &filePath)
{
    m_filePath = filePath;
    load();
}

QString AnomalyStatusStore::keyFor(qint64 id, const QString &timestampIso)
{
    return QString::number(id) + QLatin1Char('@') + timestampIso;
}

QString AnomalyStatusStore::keyFor(const QJsonObject &record)
{
    return keyFor(record.value(QStringLiteral("id")).toInt(),
                  record.value(QStringLiteral("timestamp")).toString());
}

QString AnomalyStatusStore::status(const QString &key) const
{
    return m_statusMap.value(key).toObject()
        .value(QStringLiteral("status")).toString(QLatin1String(kDetected));
}

bool AnomalyStatusStore::isResolved(const QString &key) const
{
    return status(key) == QLatin1String(kResolved);
}

QString AnomalyStatusStore::resolvedAt(const QString &key) const
{
    return m_statusMap.value(key).toObject()
        .value(QStringLiteral("resolvedAt")).toString();
}

void AnomalyStatusStore::setResolved(const QString &key, bool resolved)
{
    if (resolved) {
        QJsonObject entry = m_statusMap.value(key).toObject();
        entry.insert(QStringLiteral("status"), QLatin1String(kResolved));
        entry.insert(QStringLiteral("resolvedAt"),
                     QDateTime::currentDateTime().toString(Qt::ISODate));
        m_statusMap.insert(key, entry);
    } else {
        m_statusMap.remove(key);
    }
    save();
}

int AnomalyStatusStore::trackedCount() const
{
    return m_statusMap.size();
}

void AnomalyStatusStore::load()
{
    m_statusMap = QJsonObject();
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject())
        m_statusMap = doc.object();
}

void AnomalyStatusStore::save() const
{
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(m_statusMap).toJson(QJsonDocument::Indented));
    file.commit();
}
