#pragma once

#include "../anomaly/Anomaly.h"

#include <QAbstractTableModel>
#include <QStringList>
#include <QVector>

/**
 * @brief Модель журнала обнаруженных аномалий (со статусом
 * «обнаружено»/«решено» — последний ставит оператор).
 */
class AnomalyTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColTime, ColSeverity, ColStatus, ColDetector, ColRule,
                  ColInterface, ColSource, ColDestination, ColTitle,
                  ColumnCount };

    explicit AnomalyTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /** status: detected / resolved (см. AnomalyStatusStore); пусто = detected. */
    void addAnomaly(const Anomaly &anomaly, const QString &status = QString());
    const Anomaly *anomalyAt(int row) const;
    /** Текущий статус строки (detected/resolved). */
    QString statusAt(int row) const;
    /** Сменить статус строки («решено» ставит оператор в контекстном меню). */
    void setStatus(int row, const QString &status);
    void clear();

    static QString severityColorName(Anomaly::Severity severity);
    /** Человекочитаемое имя статуса («обнаружено»/«решено»). */
    static QString statusDisplayName(const QString &status);

private:
    QVector<Anomaly> m_anomalies;
    QStringList      m_statuses;   ///< параллельно m_anomalies: detected/resolved
};
