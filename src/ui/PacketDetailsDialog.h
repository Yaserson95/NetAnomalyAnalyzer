#pragma once

#include "../core/Packet.h"

#include <QDialog>

class QTreeWidget;
class QPlainTextEdit;

/**
 * @brief Диалог подробной информации о пакете: дерево разобранных заголовков
 * + hex-дамп сырых байтов (смещение, шестнадцатеричный вид, ASCII-колонка).
 */
class PacketDetailsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PacketDetailsDialog(const Packet &packet, QWidget *parent = nullptr);

private:
    void buildFieldTree(const Packet &packet);
    void buildHexDump(const Packet &packet);

    static QString tcpFlagsDescription(quint8 flags);
    static QString etherTypeName(quint16 type);
    static QString transportName(Packet::Transport transport);

    QTreeWidget   *m_tree = nullptr;
    QPlainTextEdit *m_hexDump = nullptr;
};
