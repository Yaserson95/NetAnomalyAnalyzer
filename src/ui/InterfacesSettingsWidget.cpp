#include "InterfacesSettingsWidget.h"
#include "../core/InterfaceManager.h"
#include "../settings/Settings.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

InterfacesSettingsWidget::InterfacesSettingsWidget(InterfaceManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Отметьте интерфейсы, трафик которых нужно захватывать. "
           "Список берётся из системы (Npcap/libpcap). «Применить» сохраняет выбор, "
           "перезапуск захвата выполняется кнопкой «Старт» в главном окне."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#666"));
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_list, 1);

    auto *buttonRow = new QHBoxLayout;
    m_refreshButton = new QPushButton(tr("Обновить список"), this);
    auto *enableAll = new QPushButton(tr("Включить все"), this);
    auto *disableAll = new QPushButton(tr("Выключить все"), this);
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addWidget(enableAll);
    buttonRow->addWidget(disableAll);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto *group = new QGroupBox(tr("Параметры захвата"), this);
    auto *form = new QFormLayout(group);
    m_filterEdit = new QLineEdit(group);
    m_filterEdit->setPlaceholderText(tr("например: tcp port 80 or udp port 53"));
    form->addRow(tr("BPF-фильтр:"), m_filterEdit);
    m_snaplenSpin = new QSpinBox(group);
    m_snaplenSpin->setRange(64, 262144);
    m_snaplenSpin->setSingleStep(1024);
    m_snaplenSpin->setSuffix(tr(" байт"));
    form->addRow(tr("Размер захвата:"), m_snaplenSpin);
    m_promiscuousCheck = new QCheckBox(tr("Смешанный режим (promiscuous)"), group);
    form->addRow(QString(), m_promiscuousCheck);
    m_autoStartCheck = new QCheckBox(tr("Начинать захват при запуске"), group);
    form->addRow(QString(), m_autoStartCheck);
    layout->addWidget(group);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setStyleSheet(QStringLiteral("color:#666"));
    layout->addWidget(m_hintLabel);

    connect(m_refreshButton, &QPushButton::clicked, this, &InterfacesSettingsWidget::onRefreshClicked);
    connect(enableAll, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_list->count(); ++i)
            m_list->item(i)->setCheckState(Qt::Checked);
    });
    connect(disableAll, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_list->count(); ++i)
            m_list->item(i)->setCheckState(Qt::Unchecked);
    });

    load();
}

void InterfacesSettingsWidget::load()
{
    if (!CaptureDevice::pcapSupported())
        m_hintLabel->setText(tr("Внимание: приложение собрано без Npcap/libpcap — "
                                "используется демо-генератор трафика."));
    else
        m_hintLabel->clear();

    const CaptureSettings &capture = Settings::instance().capture();
    m_filterEdit->setText(capture.bpfFilter);
    m_snaplenSpin->setValue(capture.snaplen);
    m_promiscuousCheck->setChecked(capture.promiscuous);
    m_autoStartCheck->setChecked(capture.autoStart);
    rebuildDeviceList();
}

void InterfacesSettingsWidget::apply()
{
    auto capture = Settings::instance().capture();
    capture.bpfFilter = m_filterEdit->text().trimmed();
    capture.snaplen = m_snaplenSpin->value();
    capture.promiscuous = m_promiscuousCheck->isChecked();
    capture.autoStart = m_autoStartCheck->isChecked();
    Settings::instance().setCapture(capture);

    if (m_manager)
        m_manager->setFilter(capture.bpfFilter);

    // Сохранение выбора интерфейсов
    if (m_manager) {
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *item = m_list->item(i);
            m_manager->setDeviceEnabled(item->data(Qt::UserRole).toString(),
                                        item->checkState() == Qt::Checked);
        }
    }
}

void InterfacesSettingsWidget::onRefreshClicked()
{
    if (m_manager)
        m_manager->refreshDevices();
    rebuildDeviceList();
}

void InterfacesSettingsWidget::rebuildDeviceList()
{
    if (!m_list || !m_manager)
        return;
    m_list->clear();
    for (const CaptureDevice &device : m_manager->devices()) {
        auto *item = new QListWidgetItem(device.displayName(), m_list);
        item->setCheckState(device.isEnabled() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, device.name());
        item->setToolTip(device.name());
        if (device.isLoopback()) {
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
            item->setToolTip(device.name() + tr(" (loopback)"));
        }
        m_list->addItem(item);
    }
}
