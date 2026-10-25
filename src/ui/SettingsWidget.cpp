#include "SettingsWidget.h"
#include "DetectionSettingsWidget.h"
#include "InterfacesSettingsWidget.h"
#include "LlmSettingsWidget.h"
#include "LoggingSettingsWidget.h"
#include "NotificationSettingsWidget.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsWidget::SettingsWidget(InterfaceManager *manager, LlmScheduler *llmScheduler,
                               QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *tabs = new QTabWidget(this);
    m_interfaces = new InterfacesSettingsWidget(manager, tabs);
    m_detection = new DetectionSettingsWidget(tabs);
    m_llm = new LlmSettingsWidget(llmScheduler, tabs);
    m_notifications = new NotificationSettingsWidget(tabs);
    m_logging = new LoggingSettingsWidget(tabs);

    tabs->addTab(m_interfaces, tr("Интерфейсы"));
    tabs->addTab(m_detection, tr("Правила аномалий"));
    tabs->addTab(m_llm, tr("LLM"));
    tabs->addTab(m_notifications, tr("Оповещения"));
    tabs->addTab(m_logging, tr("Журналирование"));
    layout->addWidget(tabs, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *resetBtn = new QPushButton(tr("Сбросить (перечитать)"), this);
    auto *applyBtn = new QPushButton(tr("Применить"), this);
    applyBtn->setDefault(true);
    buttons->addWidget(resetBtn);
    buttons->addWidget(applyBtn);
    layout->addLayout(buttons);

    connect(applyBtn, &QPushButton::clicked, this, &SettingsWidget::apply);
    connect(resetBtn, &QPushButton::clicked, this, &SettingsWidget::load);
}

void SettingsWidget::load()
{
    m_interfaces->load();
    m_detection->load();
    m_llm->load();
    m_notifications->load();
    m_logging->load();
}

void SettingsWidget::apply()
{
    m_interfaces->apply();
    m_detection->apply();
    m_llm->apply();
    m_notifications->apply();
    m_logging->apply();
}
