#include "LlmSettingsWidget.h"
#include "../llm/OpenAiCompatibleClient.h"
#include "../llm/LlmScheduler.h"
#include "../settings/Settings.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

LlmSettingsWidget::LlmSettingsWidget(LlmScheduler *scheduler, QWidget *parent)
    : QWidget(parent)
    , m_scheduler(scheduler)
{
    auto *layout = new QVBoxLayout(this);

    auto *group = new QGroupBox(tr("Подключение"), this);
    auto *form = new QFormLayout(group);

    m_enabledCheck = new QCheckBox(tr("Включить LLM-анализ (по расписанию и по запросу)"), group);
    form->addRow(QString(), m_enabledCheck);

    m_baseUrlEdit = new QLineEdit(group);
    m_baseUrlEdit->setPlaceholderText(tr("https://api.openai.com/v1"));
    form->addRow(tr("Адрес API (baseUrl):"), m_baseUrlEdit);

    m_apiKeyEdit = new QLineEdit(group);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(tr("sk-… (для локальных серверов можно не заполнять)"));
    form->addRow(tr("API-ключ:"), m_apiKeyEdit);

    m_modelEdit = new QLineEdit(group);
    m_modelEdit->setPlaceholderText(tr("gpt-4o-mini, llama3 и т.п."));
    form->addRow(tr("Модель:"), m_modelEdit);

    m_intervalSpin = new QSpinBox(group);
    m_intervalSpin->setRange(0, 1440);
    m_intervalSpin->setSuffix(tr(" мин"));
    m_intervalSpin->setSpecialValueText(tr("Выключено"));
    form->addRow(tr("Отправлять в LLM каждые:"), m_intervalSpin);

    m_maxTokensSpin = new QSpinBox(group);
    m_maxTokensSpin->setRange(64, 8192);
    m_maxTokensSpin->setSingleStep(64);
    form->addRow(tr("Максимум токенов ответа:"), m_maxTokensSpin);

    m_temperatureSpin = new QDoubleSpinBox(group);
    m_temperatureSpin->setRange(0.0, 2.0);
    m_temperatureSpin->setSingleStep(0.1);
    form->addRow(tr("Температура:"), m_temperatureSpin);

    m_contextLinesSpin = new QSpinBox(group);
    m_contextLinesSpin->setRange(5, 1000);
    form->addRow(tr("Записей журнала в запросе:"), m_contextLinesSpin);

    m_budgetTokensSpin = new QSpinBox(group);
    m_budgetTokensSpin->setRange(500, 128000);
    m_budgetTokensSpin->setSingleStep(500);
    m_budgetTokensSpin->setToolTip(tr("Если собранный контекст больше бюджета, запрос "
                                     "автоматически делится на части и отправляется "
                                     "последовательно, а вердикты объединяются."));
    form->addRow(tr("Бюджет контекста (токенов):"), m_budgetTokensSpin);

    layout->addWidget(group);

    auto *promptGroup = new QGroupBox(tr("Системный промпт"), this);
    auto *promptLayout = new QVBoxLayout(promptGroup);
    m_systemPromptEdit = new QPlainTextEdit(promptGroup);
    m_systemPromptEdit->setMinimumHeight(110);
    promptLayout->addWidget(m_systemPromptEdit);
    layout->addWidget(promptGroup, 1);

    auto *buttonRow = new QHBoxLayout;
    m_sendNowButton = new QPushButton(tr("Отправить сейчас"), this);
    m_sendNowButton->setToolTip(
        tr("Немедленно собрать журналы (сводка сети + последние аномалии) "
           "и отправить промпт в LLM, минуя расписание."));
    m_testButton = new QPushButton(tr("Тест соединения"), this);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#666"));
    buttonRow->addWidget(m_sendNowButton);
    buttonRow->addWidget(m_testButton);
    buttonRow->addWidget(m_statusLabel, 1);
    layout->addLayout(buttonRow);

    connect(m_testButton, &QPushButton::clicked, this, &LlmSettingsWidget::onTestClicked);
    connect(m_sendNowButton, &QPushButton::clicked, this, &LlmSettingsWidget::onSendNowClicked);

    load();
}

void LlmSettingsWidget::load()
{
    const LlmSettings &llm = Settings::instance().llm();
    m_enabledCheck->setChecked(llm.enabled);
    m_baseUrlEdit->setText(llm.baseUrl);
    m_apiKeyEdit->setText(llm.apiKey);
    m_modelEdit->setText(llm.model);
    m_intervalSpin->setValue(llm.intervalMin);
    m_maxTokensSpin->setValue(llm.maxTokens);
    m_temperatureSpin->setValue(llm.temperature);
    m_contextLinesSpin->setValue(llm.contextLines);
    m_budgetTokensSpin->setValue(llm.contextBudgetTokens);
    m_systemPromptEdit->setPlainText(llm.systemPrompt);
}

void LlmSettingsWidget::apply()
{
    LlmSettings llm = Settings::instance().llm();
    llm.enabled = m_enabledCheck->isChecked();
    llm.baseUrl = m_baseUrlEdit->text().trimmed();
    llm.apiKey = m_apiKeyEdit->text().trimmed();
    llm.model = m_modelEdit->text().trimmed();
    llm.intervalMin = m_intervalSpin->value();
    llm.maxTokens = m_maxTokensSpin->value();
    llm.temperature = m_temperatureSpin->value();
    llm.contextLines = m_contextLinesSpin->value();
    llm.contextBudgetTokens = m_budgetTokensSpin->value();
    llm.systemPrompt = m_systemPromptEdit->toPlainText();
    Settings::instance().setLlm(llm);
}

void LlmSettingsWidget::onSendNowClicked()
{
    // Сохранить текущие поля, затем сразу отправить сводку + журналы в LLM.
    apply();
    if (!m_scheduler) {
        m_statusLabel->setText(tr("Планировщик LLM недоступен"));
        return;
    }
    m_statusLabel->setStyleSheet(QStringLiteral("color:#666"));
    m_statusLabel->setText(tr("Запрос отправлен — смотрите вкладку «Журнал LLM»"));
    m_scheduler->requestManualAnalysis();
}

void LlmSettingsWidget::onTestClicked()
{
    apply();
    auto *client = new OpenAiCompatibleClient(this);
    const LlmSettings llm = Settings::instance().llm();
    m_statusLabel->setText(tr("Выполняется тест…"));
    m_testButton->setEnabled(false);

    connect(client, &OpenAiCompatibleClient::responseReceived, this,
            [this, client](const QString &response, qint64 elapsedMs) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#277d38"));
                m_statusLabel->setText(tr("OK — ответ «%1» за %2 мс")
                                           .arg(response.left(40), QString::number(elapsedMs)));
                m_testButton->setEnabled(true);
                client->deleteLater();
            });
    connect(client, &OpenAiCompatibleClient::requestFailed, this,
            [this, client](const QString &error) {
                m_statusLabel->setStyleSheet(QStringLiteral("color:#c0392b"));
                m_statusLabel->setText(error.left(160));
                m_testButton->setEnabled(true);
                client->deleteLater();
            });
    client->testConnection(llm);
}
