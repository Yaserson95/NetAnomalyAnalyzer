#include "PacketTableModel.h"
#include "../core/CaptureDevice.h"

#include <QColor>
#include <QFont>

PacketTableModel::PacketTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int PacketTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_packets.size();
}

int PacketTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PacketTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColTime:       return tr("Время");
    case ColInterface:  return tr("Интерфейс");
    case ColSource:     return tr("Источник");
    case ColDestination:return tr("Назначение");
    case ColProtocol:   return tr("Протокол");
    case ColLength:     return tr("Длина");
    case ColInfo:       return tr("Информация");
    }
    return {};
}

QVariant PacketTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_packets.size())
        return {};
    const Packet &packet = m_packets.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTime:
            return packet.timestamp().toString(QStringLiteral("HH:mm:ss.zzz"));
        case ColInterface:
            return CaptureDevice::friendlyName(packet.interfaceName());
        case ColSource: {
            if (packet.sourcePort())
                return packet.sourceIp() + QLatin1Char(':') + QString::number(packet.sourcePort());
            return packet.sourceIp();
        }
        case ColDestination: {
            if (packet.destinationPort())
                return packet.destinationIp() + QLatin1Char(':') + QString::number(packet.destinationPort());
            return packet.destinationIp();
        }
        case ColProtocol:
            return packet.protocolName();
        case ColLength:
            return packet.capturedLength();
        case ColInfo: {
            QString info = packet.summary();
            if (!packet.payload().isEmpty())
                info += QStringLiteral("  «%1»").arg(packet.payloadPreview(40));
            return info;
        }
        }
    } else if (role == Qt::ToolTipRole) {
        if (index.column() == ColInterface)
            return packet.interfaceName();
    } else if (role == Qt::ForegroundRole) {
        if (packet.transport() == Packet::Transport::ARP)
            return QColor(150, 110, 0);
        if (packet.isTcpSyn())
            return QColor(160, 40, 40);
        if (packet.appLayer() == Packet::AppLayer::DNS)
            return QColor(30, 90, 160);
    } else if (role == Qt::TextAlignmentRole && index.column() == ColLength) {
        return int(Qt::AlignRight | Qt::AlignVCenter);
    }
    return {};
}

void PacketTableModel::appendPacket(const Packet &packet)
{
    const int rows = m_packets.size();
    const bool trim = rows >= m_maxRows;

    if (trim) {
        // Сдвигаем буфер пачками, чтобы не вызывать removeRow на каждый пакет.
        const int drop = qMax(1, m_maxRows / 10);
        beginRemoveRows(QModelIndex(), 0, drop - 1);
        m_packets.remove(0, drop);
        endRemoveRows();
    }

    const int newRow = m_packets.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_packets.append(packet);
    endInsertRows();
}

void PacketTableModel::setMaxRows(int maxRows)
{
    maxRows = qMax(1, maxRows);
    if (maxRows == m_maxRows)
        return;
    m_maxRows = maxRows;

    const int excess = m_packets.size() - m_maxRows;
    if (excess > 0) {
        beginRemoveRows(QModelIndex(), 0, excess - 1);
        m_packets.remove(0, excess);
        endRemoveRows();
    }
}

const Packet *PacketTableModel::packetAt(int row) const
{
    if (row < 0 || row >= m_packets.size())
        return nullptr;
    return &m_packets.at(row);
}

void PacketTableModel::clear()
{
    beginResetModel();
    m_packets.clear();
    endResetModel();
}
