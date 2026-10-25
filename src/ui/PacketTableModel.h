#pragma once

#include "../core/Packet.h"

#include <QAbstractTableModel>
#include <QVector>

/**
 * @brief Модель потока перехваченных пакетов.
 * Хранит ограниченный кольцевой буфер последних пакетов.
 */
class PacketTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColTime, ColInterface, ColSource, ColDestination,
                  ColProtocol, ColLength, ColInfo, ColumnCount };

    explicit PacketTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /** Добавить пакет (потокобезопасно вызывать из главного потока). */
    void appendPacket(const Packet &packet);
    const Packet *packetAt(int row) const;

    /** Установить глубину буфера: лишние (самые старые) строки удаляются сразу. */
    void setMaxRows(int maxRows);
    int maxRows() const { return m_maxRows; }
    void clear();

private:
    QVector<Packet> m_packets;
    int m_maxRows = 300;
};
