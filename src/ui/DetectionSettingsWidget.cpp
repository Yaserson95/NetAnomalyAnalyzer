#include "DetectionSettingsWidget.h"
#include "../settings/Settings.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

DetectionSettingsWidget::DetectionSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *group = new QGroupBox(tr("Включённые правила"), this);
    auto *checksLayout = new QGridLayout(group);
    m_portScanCheck = new QCheckBox(tr("Сканирование портов"), group);
    m_synFloodCheck = new QCheckBox(tr("SYN-флуд"), group);
    m_udpFloodCheck = new QCheckBox(tr("UDP-флуд"), group);
    m_icmpFloodCheck = new QCheckBox(tr("ICMP-флуд"), group);
    m_broadcastCheck = new QCheckBox(tr("Broadcast-шторм"), group);
    m_suspiciousPortCheck = new QCheckBox(tr("Подозрительные порты"), group);
    m_payloadCheck = new QCheckBox(tr("Сигнатуры в payload"), group);
    checksLayout->addWidget(m_portScanCheck, 0, 0);
    checksLayout->addWidget(m_synFloodCheck, 0, 1);
    checksLayout->addWidget(m_udpFloodCheck, 1, 0);
    checksLayout->addWidget(m_icmpFloodCheck, 1, 1);
    checksLayout->addWidget(m_broadcastCheck, 2, 0);
    checksLayout->addWidget(m_suspiciousPortCheck, 2, 1);
    checksLayout->addWidget(m_payloadCheck, 3, 0);
    layout->addWidget(group);

    auto *thresholds = new QGroupBox(tr("Пороги"), this);
    auto *form = new QFormLayout(thresholds);
    m_portScanThreshold = new QSpinBox(thresholds);
    m_portScanThreshold->setRange(2, 1000);
    form->addRow(tr("Сканирование: уникальных портов у цели:"), m_portScanThreshold);
    m_portScanWindow = new QSpinBox(thresholds);
    m_portScanWindow->setRange(1, 300);
    m_portScanWindow->setSuffix(tr(" сек"));
    form->addRow(tr("Сканирование: окно анализа:"), m_portScanWindow);
    m_synFloodPps = new QSpinBox(thresholds);
    m_synFloodPps->setRange(10, 100000);
    m_synFloodPps->setSingleStep(10);
    form->addRow(tr("SYN-флуд, SYN/сек:"), m_synFloodPps);
    m_udpFloodPps = new QSpinBox(thresholds);
    m_udpFloodPps->setRange(10, 500000);
    form->addRow(tr("UDP-флуд, пак/сек:"), m_udpFloodPps);
    m_icmpFloodPps = new QSpinBox(thresholds);
    m_icmpFloodPps->setRange(10, 100000);
    form->addRow(tr("ICMP-флуд, пак/сек:"), m_icmpFloodPps);
    m_broadcastPps = new QSpinBox(thresholds);
    m_broadcastPps->setRange(10, 100000);
    form->addRow(tr("Broadcast, пак/сек:"), m_broadcastPps);
    m_cooldown = new QSpinBox(thresholds);
    m_cooldown->setRange(1, 3600);
    m_cooldown->setSuffix(tr(" сек"));
    form->addRow(tr("Повтор оповещения не чаще:"), m_cooldown);
    layout->addWidget(thresholds);

    auto *lists = new QGroupBox(tr("Списки"), this);
    auto *listsLayout = new QVBoxLayout(lists);
    listsLayout->addWidget(new QLabel(tr("Подозрительные порты (через запятую):"), lists));
    m_portsEdit = new QLineEdit(lists);
    listsLayout->addWidget(m_portsEdit);
    listsLayout->addWidget(new QLabel(
        tr("Сигнатуры в полезной нагрузке (каждая с новой строки):"), lists));
    m_signaturesEdit = new QTextEdit(lists);
    m_signaturesEdit->setFixedHeight(110);
    listsLayout->addWidget(m_signaturesEdit);
    layout->addWidget(lists);
    layout->addStretch();

    load();
}

void DetectionSettingsWidget::load()
{
    const DetectionSettings &d = Settings::instance().detection();
    m_portScanCheck->setChecked(d.enablePortScan);
    m_synFloodCheck->setChecked(d.enableSynFlood);
    m_udpFloodCheck->setChecked(d.enableUdpFlood);
    m_icmpFloodCheck->setChecked(d.enableIcmpFlood);
    m_broadcastCheck->setChecked(d.enableBroadcast);
    m_suspiciousPortCheck->setChecked(d.enableSuspiciousPort);
    m_payloadCheck->setChecked(d.enablePayload);

    m_portScanThreshold->setValue(d.portScanPortThreshold);
    m_portScanWindow->setValue(d.portScanWindowSec);
    m_synFloodPps->setValue(d.synFloodPps);
    m_udpFloodPps->setValue(d.udpFloodPps);
    m_icmpFloodPps->setValue(d.icmpFloodPps);
    m_broadcastPps->setValue(d.broadcastPps);
    m_cooldown->setValue(d.anomalyCooldownSec);

    m_portsEdit->setText(d.suspiciousPorts.join(QLatin1String(", ")));
    m_signaturesEdit->setPlainText(d.payloadSignatures.join(QLatin1Char('\n')));
}

void DetectionSettingsWidget::apply()
{
    DetectionSettings d = Settings::instance().detection();
    d.enablePortScan = m_portScanCheck->isChecked();
    d.enableSynFlood = m_synFloodCheck->isChecked();
    d.enableUdpFlood = m_udpFloodCheck->isChecked();
    d.enableIcmpFlood = m_icmpFloodCheck->isChecked();
    d.enableBroadcast = m_broadcastCheck->isChecked();
    d.enableSuspiciousPort = m_suspiciousPortCheck->isChecked();
    d.enablePayload = m_payloadCheck->isChecked();

    d.portScanPortThreshold = m_portScanThreshold->value();
    d.portScanWindowSec = m_portScanWindow->value();
    d.synFloodPps = m_synFloodPps->value();
    d.udpFloodPps = m_udpFloodPps->value();
    d.icmpFloodPps = m_icmpFloodPps->value();
    d.broadcastPps = m_broadcastPps->value();
    d.anomalyCooldownSec = m_cooldown->value();

    d.suspiciousPorts.clear();
    const QStringList ports = m_portsEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &port : ports) {
        const QString trimmed = port.trimmed();
        if (!trimmed.isEmpty())
            d.suspiciousPorts << trimmed;
    }

    d.payloadSignatures.clear();
    const QStringList lines = m_signaturesEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            d.payloadSignatures << trimmed;
    }

    Settings::instance().setDetection(d);
}
