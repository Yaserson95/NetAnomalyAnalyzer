#include "AnomalyTableModel.h"
#include "../anomaly/AnomalyStatusStore.h"
#include "../core/CaptureDevice.h"

#include <QColor>

QString AnomalyTableModel::severityColorName(Anomaly::Severity severity)
{
    switch (severity) {
    case Anomaly::Severity::Info:     return QStringLiteral("#7f8c8d");
    case Anomaly::Severity::Warning:  return QStringLiteral("#d68910");
    case Anomaly::Severity::Critical: return QStringLiteral("#c0392b");
    }
    return {};
}

AnomalyTableModel::AnomalyTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int AnomalyTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_anomalies.size();
}

int AnomalyTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant AnomalyTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColTime:        return tr("Время");
    case ColSeverity:    return tr("Уровень");
    case ColStatus:      return tr("Статус");
    case ColDetector:    return tr("Обнаружение");
    case ColRule:        return tr("Правило");
    case ColInterface:   return tr("Интерфейс");
    case ColSource:      return tr("Источник");
    case ColDestination: return tr("Назначение");
    case ColTitle:       return tr("Описание");
    }
    return {};
}

QVariant AnomalyTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_anomalies.size())
        return {};
    const Anomaly &anomaly = m_anomalies.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTime:        return anomaly.timestamp().toString(QStringLiteral("dd.MM HH:mm:ss.zzz"));
        case ColSeverity:    return anomaly.severityName();
        case ColStatus:      return statusDisplayName(statusAt(index.row()));
        case ColDetector:    return anomaly.detector().isEmpty()
                                      ? Anomaly::defaultDetectorName()
                                      : anomaly.detector();
        case ColRule:        return anomaly.ruleName();
        case ColInterface:   return CaptureDevice::friendlyName(anomaly.interfaceName());
        case ColSource:      return anomaly.sourceIp();
        case ColDestination: return anomaly.destinationIp();
        case ColTitle:       return anomaly.title();
        }
    } else if (role == Qt::ForegroundRole) {
        // Разрешённые аномалии приглушаем; цвет строки — по уровню
        if (index.column() == ColStatus
            && statusAt(index.row()) == QLatin1String(AnomalyStatusStore::kResolved))
            return QColor(127, 140, 141);
        return QColor(severityColorName(anomaly.severity()));
    } else if (role == Qt::ToolTipRole) {
        QString tip = QStringLiteral("%1\n%2\nОбнаружено: %3\n%4 → %5\n%6")
                          .arg(anomaly.title(), anomaly.ruleName(),
                               anomaly.detector().isEmpty()
                                   ? Anomaly::defaultDetectorName()
                                   : anomaly.detector(),
                               anomaly.sourceIp(), anomaly.destinationIp(),
                               anomaly.description());
        if (anomaly.packetCount() > 1)
            tip += QStringLiteral("\nПакетов: %1").arg(anomaly.packetCount());
        tip += QStringLiteral("\nСтатус: %1").arg(statusDisplayName(statusAt(index.row())));
        return tip;
    }
    return {};
}

void AnomalyTableModel::addAnomaly(const Anomaly &anomaly, const QString &status)
{
    const int row = m_anomalies.size();
    beginInsertRows(QModelIndex(), row, row);
    m_anomalies.append(anomaly);
    m_statuses.append(status.isEmpty()
                          ? QLatin1String(AnomalyStatusStore::kDetected)
                          : status);
    endInsertRows();
}

const Anomaly *AnomalyTableModel::anomalyAt(int row) const
{
    if (row < 0 || row >= m_anomalies.size())
        return nullptr;
    return &m_anomalies.at(row);
}

QString AnomalyTableModel::statusAt(int row) const
{
    if (row < 0 || row >= m_statuses.size())
        return QLatin1String(AnomalyStatusStore::kDetected);
    return m_statuses.at(row);
}

void AnomalyTableModel::setStatus(int row, const QString &status)
{
    if (row < 0 || row >= m_statuses.size())
        return;
    m_statuses[row] = status;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

QString AnomalyTableModel::statusDisplayName(const QString &status)
{
    if (status == QLatin1String(AnomalyStatusStore::kResolved))
        return QObject::tr("решено");
    return QObject::tr("обнаружено");
}

void AnomalyTableModel::clear()
{
    beginResetModel();
    m_anomalies.clear();
    m_statuses.clear();
    endResetModel();
}
