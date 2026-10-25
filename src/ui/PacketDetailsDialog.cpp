#include "PacketDetailsDialog.h"
#include "../core/CaptureDevice.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

PacketDetailsDialog::PacketDetailsDialog(const Packet &packet, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Пакет от %1 — %2")
                       .arg(packet.timestamp().toString(QStringLiteral("HH:mm:ss.zzz")),
                            packet.protocolName()));
    resize(780, 560);

    auto *layout = new QVBoxLayout(this);

    auto *summary = new QLabel(packet.summary(), this);
    QFont bold = summary->font();
    bold.setBold(true);
    summary->setFont(bold);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    // --- Дерево полей ---
    m_tree = new QTreeWidget(splitter);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({ tr("Поле"), tr("Значение") });
    m_tree->header()->setStretchLastSection(true);
    m_tree->setIndentation(16);
    m_tree->setRootIsDecorated(true);
    buildFieldTree(packet);
    m_tree->expandAll();

    // --- Hex-дамп ---
    auto *dumpPanel = new QWidget(splitter);
    auto *dumpLayout = new QVBoxLayout(dumpPanel);
    dumpLayout->setContentsMargins(0, 0, 0, 0);
    auto *dumpLabel = new QLabel(tr("Дамп пакета (hex):"), dumpPanel);
    dumpLayout->addWidget(dumpLabel);
    m_hexDump = new QPlainTextEdit(dumpPanel);
    m_hexDump->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(9);
    m_hexDump->setFont(mono);
    m_hexDump->setLineWrapMode(QPlainTextEdit::NoWrap);
    buildHexDump(packet);
    dumpLayout->addWidget(m_hexDump);

    splitter->addWidget(m_tree);
    splitter->addWidget(dumpPanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *copyBtn = buttons->addButton(tr("Копировать дамп"),
                                              QDialogButtonBox::ActionRole);
    QPushButton *closeBtn = buttons->addButton(tr("Закрыть"),
                                               QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    connect(copyBtn, &QPushButton::clicked, this, [this] {
        m_hexDump->selectAll();
        m_hexDump->copy();
        m_hexDump->moveCursor(QTextCursor::Start);
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void PacketDetailsDialog::buildFieldTree(const Packet &packet)
{
    auto addTop = [this](const QString &title) {
        auto *item = new QTreeWidgetItem(m_tree, { title });
        item->setFirstColumnSpanned(true);
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        return item;
    };
    auto addField = [](QTreeWidgetItem *parent, const QString &name, const QString &value) {
        new QTreeWidgetItem(parent, { name, value });
    };

    // --- Кадр ---
    QTreeWidgetItem *frame = addTop(tr("Кадр %1: %2 байт на wire, %3 байт захвачено")
                                        .arg(packet.capturedLength())
                                        .arg(packet.originalLength())
                                        .arg(packet.rawFrame().size()));
    addField(frame, tr("Время прихода"),
             packet.timestamp().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    addField(frame, tr("Интерфейс"),
             QStringLiteral("%1 (%2)")
                 .arg(CaptureDevice::friendlyName(packet.interfaceName()),
                      packet.interfaceName()));

    // --- Ethernet ---
    if (!packet.sourceMac().isEmpty() || !packet.destinationMac().isEmpty()) {
        QTreeWidgetItem *eth = addTop(tr("Ethernet II"));
        addField(eth, tr("Адрес назначения"), packet.destinationMac());
        addField(eth, tr("Адрес источника"), packet.sourceMac());
        addField(eth, tr("Тип"), QStringLiteral("0x%1 (%2)")
                                     .arg(packet.etherType(), 4, 16, QLatin1Char('0'))
                                     .arg(etherTypeName(packet.etherType())));
    }

    // --- Сетевой уровень ---
    if (!packet.sourceIp().isEmpty()) {
        const bool ipv6 = packet.sourceIp().contains(QLatin1Char(':'));
        QTreeWidgetItem *ip = addTop(ipv6 ? tr("IPv6") : tr("IPv4"));
        addField(ip, tr("Адрес источника"), packet.sourceIp());
        addField(ip, tr("Адрес назначения"), packet.destinationIp());
        if (packet.ttl() > 0) {
            addField(ip, ipv6 ? tr("Hlim") : tr("TTL"), QString::number(packet.ttl()));
        }
        if (packet.isBroadcast())
            addField(ip, tr("Назначение"), tr("Broadcast (ff:ff:ff:ff:ff:ff)"));
        else if (packet.isMulticast())
            addField(ip, tr("Назначение"), tr("Multicast"));
    }

    // --- Транспортный уровень ---
    switch (packet.transport()) {
    case Packet::Transport::TCP: {
        QTreeWidgetItem *tcp = addTop(tr("Transmission Control Protocol"));
        addField(tcp, tr("Порт источника"), QString::number(packet.sourcePort()));
        addField(tcp, tr("Порт назначения"), QString::number(packet.destinationPort()));
        addField(tcp, tr("Флаги"),
                 QStringLiteral("0x%1 [%2]")
                     .arg(packet.tcpFlags(), 2, 16, QLatin1Char('0'))
                     .arg(tcpFlagsDescription(packet.tcpFlags())));
        addField(tcp, tr("SYN"), packet.isTcpSyn() ? tr("Да") : tr("Нет"));
        addField(tcp, tr("ACK"), packet.isTcpAck() ? tr("Да") : tr("Нет"));
        addField(tcp, tr("RST"), packet.isTcpRst() ? tr("Да") : tr("Нет"));
        addField(tcp, tr("FIN"), packet.isTcpFin() ? tr("Да") : tr("Нет"));
        break;
    }
    case Packet::Transport::UDP: {
        QTreeWidgetItem *udp = addTop(tr("User Datagram Protocol"));
        addField(udp, tr("Порт источника"), QString::number(packet.sourcePort()));
        addField(udp, tr("Порт назначения"), QString::number(packet.destinationPort()));
        addField(udp, tr("Длина"), QStringLiteral("%1 байт")
                                        .arg(packet.payload().size() + 8));
        break;
    }
    case Packet::Transport::ICMP:
    case Packet::Transport::ICMPv6: {
        QTreeWidgetItem *icmp = addTop(
            packet.transport() == Packet::Transport::ICMP ? tr("Internet Control Message Protocol")
                                                          : tr("ICMPv6"));
        addField(icmp, tr("Тип/код (raw)"), QString::number(packet.ttl()));
        break;
    }
    case Packet::Transport::ARP: {
        QTreeWidgetItem *arp = addTop(tr("Address Resolution Protocol"));
        addField(arp, tr("Операция"), packet.ttl() == 2 ? tr("Ответ") : tr("Запрос"));
        addField(arp, tr("Отправитель"), QStringLiteral("%1 (%2)")
                                             .arg(packet.sourceIp(), packet.sourceMac()));
        addField(arp, tr("Получатель"), QStringLiteral("%1 (%2)")
                                             .arg(packet.destinationIp(), packet.destinationMac()));
        break;
    }
    default:
        break;
    }

    // --- Прикладной уровень / данные ---
    if (packet.appLayer() != Packet::AppLayer::None) {
        QTreeWidgetItem *app = addTop(tr("Прикладной уровень"));
        addField(app, tr("Протокол"), packet.appLayerName());
    }
    if (!packet.payload().isEmpty()) {
        QTreeWidgetItem *data = addTop(tr("Данные (payload)"));
        addField(data, tr("Размер"), QStringLiteral("%1 байт").arg(packet.payload().size()));
        addField(data, tr("Предпросмотр"), packet.payloadPreview(200));
    }
}

void PacketDetailsDialog::buildHexDump(const Packet &packet)
{
    const QByteArray raw = packet.rawFrame();
    if (raw.isEmpty()) {
        m_hexDump->setPlainText(tr("(сырые данные кадра не сохранены для этого пакета)"));
        return;
    }

    QString out;
    out.reserve(raw.size() * 5);
    for (int offset = 0; offset < raw.size(); offset += 16) {
        out += QStringLiteral("%1  ").arg(offset, 4, 16, QLatin1Char('0'));

        QString ascii;
        for (int i = 0; i < 16; ++i) {
            if (offset + i < raw.size()) {
                const uchar byte = static_cast<uchar>(raw.at(offset + i));
                out += QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0'));
                ascii += (byte >= 0x20 && byte < 0x7F) ? QChar(byte) : QLatin1Char('.');
            } else {
                out += QStringLiteral("   ");
                ascii += QLatin1Char(' ');
            }
            if (i == 7)
                out += QLatin1Char(' ');
        }
        out += QStringLiteral(" |") + ascii + QStringLiteral("|\n");
    }
    m_hexDump->setPlainText(out);
}

QString PacketDetailsDialog::tcpFlagsDescription(quint8 flags)
{
    QStringList names;
    if (flags & 0x01) names << QStringLiteral("FIN");
    if (flags & 0x02) names << QStringLiteral("SYN");
    if (flags & 0x04) names << QStringLiteral("RST");
    if (flags & 0x08) names << QStringLiteral("PSH");
    if (flags & 0x10) names << QStringLiteral("ACK");
    if (flags & 0x20) names << QStringLiteral("URG");
    if (flags & 0x40) names << QStringLiteral("ECE");
    if (flags & 0x80) names << QStringLiteral("CWR");
    return names.isEmpty() ? QStringLiteral("—") : names.join(QLatin1Char(' '));
}

QString PacketDetailsDialog::etherTypeName(quint16 type)
{
    switch (type) {
    case 0x0800: return QStringLiteral("IPv4");
    case 0x0806: return QStringLiteral("ARP");
    case 0x86DD: return QStringLiteral("IPv6");
    case 0x8100: return QStringLiteral("802.1Q VLAN");
    case 0x8847: return QStringLiteral("MPLS");
    case 0x88CC: return QStringLiteral("LLDP");
    default:     return tr("неизвестно");
    }
}

QString PacketDetailsDialog::transportName(Packet::Transport transport)
{
    switch (transport) {
    case Packet::Transport::TCP:     return QStringLiteral("TCP");
    case Packet::Transport::UDP:     return QStringLiteral("UDP");
    case Packet::Transport::ICMP:    return QStringLiteral("ICMP");
    case Packet::Transport::ICMPv6:  return QStringLiteral("ICMPv6");
    case Packet::Transport::ARP:     return QStringLiteral("ARP");
    default:                         return tr("неизвестно");
    }
}
