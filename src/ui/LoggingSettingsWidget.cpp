#include "LoggingSettingsWidget.h"
#include "../settings/Settings.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

LoggingSettingsWidget::LoggingSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *group = new QGroupBox(tr("Файлы журналов"), this);
    auto *form = new QFormLayout(group);

    m_logPacketsCheck = new QCheckBox(
        tr("Записывать каждый пакет в packets-*.jsonl (может занять много места)"), group);
    form->addRow(QString(), m_logPacketsCheck);

    m_logDirEdit = new QLineEdit(group);
    auto *dirRow = new QHBoxLayout;
    auto *browse = new QPushButton(tr("Обзор…"), group);
    dirRow->addWidget(m_logDirEdit, 1);
    dirRow->addWidget(browse);
    form->addRow(tr("Каталог логов:"), dirRow);

    m_maxSizeSpin = new QSpinBox(group);
    m_maxSizeSpin->setRange(1, 1024);
    m_maxSizeSpin->setSuffix(tr(" МБ"));
    form->addRow(tr("Ротация файла по размеру:"), m_maxSizeSpin);

    layout->addWidget(group);
    layout->addWidget(new QLabel(
        tr("Файлы: anomalies.jsonl — аномалии (контекст для LLM), "
           "anomaly-status.json — отметки «решено» оператором, "
           "notifications.jsonl — оповещения, llm-journal.jsonl — обращения к LLM, "
           "errors.jsonl — ошибки."), this));
    layout->addStretch();

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Выберите каталог логов"), m_logDirEdit->text());
        if (!dir.isEmpty())
            m_logDirEdit->setText(dir);
    });

    load();
}

void LoggingSettingsWidget::load()
{
    const LoggingSettings &logging = Settings::instance().logging();
    m_logPacketsCheck->setChecked(logging.logPackets);
    m_logDirEdit->setText(logging.logDir);
    m_maxSizeSpin->setValue(logging.maxFileMb);
}

void LoggingSettingsWidget::apply()
{
    LoggingSettings logging = Settings::instance().logging();
    logging.logPackets = m_logPacketsCheck->isChecked();
    const QString dir = m_logDirEdit->text().trimmed();
    logging.logDir = dir.isEmpty() ? Settings::defaultLogDir() : dir;
    logging.maxFileMb = m_maxSizeSpin->value();
    Settings::instance().setLogging(logging);
}
