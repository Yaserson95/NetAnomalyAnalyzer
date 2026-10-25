#pragma once

#include "../core/FilterCondition.h"

#include <QSortFilterProxyModel>
#include <QVector>

/**
 * @brief Прокси-модель: показывает только пакеты, удовлетворяющие
 * условиям фильтра (все условия объединяются по «И»).
 */
class PacketFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit PacketFilterProxy(QObject *parent = nullptr);

    /** Условия фильтра; пустой список — показываются все пакеты. */
    void setConditions(const QVector<FilterCondition> &conditions);
    const QVector<FilterCondition> &conditions() const { return m_conditions; }
    bool hasConditions() const { return !m_conditions.isEmpty(); }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QVector<FilterCondition> m_conditions;
};
