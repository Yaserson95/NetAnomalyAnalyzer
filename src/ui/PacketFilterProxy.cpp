#include "PacketFilterProxy.h"

#include "PacketTableModel.h"

PacketFilterProxy::PacketFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void PacketFilterProxy::setConditions(const QVector<FilterCondition> &conditions)
{
    if (m_conditions == conditions)
        return;
    m_conditions = conditions;
    invalidateRowsFilter();
}

bool PacketFilterProxy::filterAcceptsRow(int sourceRow,
                                         const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent)
    if (m_conditions.isEmpty())
        return true;
    const auto *model = qobject_cast<const PacketTableModel *>(sourceModel());
    if (!model)
        return true;
    const Packet *packet = model->packetAt(sourceRow);
    if (!packet)
        return false;
    for (const FilterCondition &condition : m_conditions) {
        if (!condition.matchesPacket(*packet))
            return false;
    }
    return true;
}
