#include "MainWindow.h"

#include "AnomalyTableModel.h"
#include "FilterDialog.h"
#include "GroupingWidget.h"
#include "HelpDialog.h"
#include "LlmJournalModel.h"
#include "LogViewerWidget.h"
#include "PacketDetailsDialog.h"
#include "PacketFilterProxy.h"
#include "PacketTableModel.h"
#include "SettingsWidget.h"
#include "StatsWidget.h"
#include "../anomaly/AnomalyDetector.h"
#include "../anomaly/AnomalyStatusStore.h"
#include "../app/AppController.h"
#include "../core/CaptureDevice.h"
#include "../core/InterfaceManager.h"
#include "../llm/LlmScheduler.h"
#include "../logging/LogWriter.h"
#include "../notify/NotificationManager.h"
#include "../settings/Settings.h"
#include "../stats/StatisticsCollector.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <utility>

MainWindow *MainWindow::create(AppController *controller, QWidget *parent)
{
    auto *window = new MainWindow(controller->interfaces(), controller->detector(),
                                  controller->statistics(),
                                  controller->notifications(),
                                  controller->llmScheduler(),
                                  controller->logWriter(), parent);
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    // История журнала аномалий со статусами «обнаружено»/«решено»
    window->preloadAnomalies(controller->logWriter()->tailAnomalies(500));
    // Поток пакетов в таблицу GUI подключается непосредственно в MainWindow.
    return window;
}

MainWindow::MainWindow(InterfaceManager *interfaces,
                       AnomalyDetector *detector,
                       StatisticsCollector *statistics,
                       NotificationManager *notifications,
                       LlmScheduler *llmScheduler,
                       LogWriter *logWriter,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_interfaces(interfaces)
    , m_detector(detector)
    , m_statistics(statistics)
    , m_notifications(notifications)
    , m_llmScheduler(llmScheduler)
    , m_logWriter(logWriter)
{
    setWindowTitle(tr("NetAnomalyAnalyzer — анализатор аномалий в сети"));
    setWindowIcon(QIcon(QStringLiteral(":/appicon/app.ico")));
    resize(1280, 800);

    m_packetModel = new PacketTableModel(this);
    m_packetModel->setMaxRows(Settings::instance().capture().maxTablePackets);
    m_anomalyModel = new AnomalyTableModel(this);
    m_journalModel = new LlmJournalModel(this);
    m_journalModel->setJournal(m_llmScheduler->journal());

    buildUi();
    buildToolbar();
    buildStatusBar();
    buildTray();

    // --- маршрутизация пакетов ---
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(100);
    connect(m_flushTimer, &QTimer::timeout, this, [this] {
        if (m_pendingPackets.isEmpty())
            return;
        m_packetModel->setMaxRows(Settings::instance().capture().maxTablePackets);
        for (const Packet &packet : std::as_const(m_pendingPackets))
            m_packetModel->appendPacket(packet);
        if (m_tabs->currentWidget() == m_packetView->parentWidget())
            m_packetView->scrollToBottom();
        if (m_tabs->currentWidget() == m_groupingWidget)
            m_groupingWidget->refresh();   // живой пересчёт групп
        m_pendingPackets.clear();
    });
    m_flushTimer->start();

    connect(m_interfaces, &InterfaceManager::packetCaptured,
            this, &MainWindow::onPacket, Qt::QueuedConnection);
    connect(m_interfaces, &InterfaceManager::captureStateChanged, this, [this](bool) {
        updateCaptureActions();
    });
    connect(m_interfaces, &InterfaceManager::captureError,
            this, &MainWindow::onCaptureError);

    connect(m_detector, &AnomalyDetector::anomalyDetected,
            this, &MainWindow::onAnomaly, Qt::QueuedConnection);
    // Аномалии, найденные только LLM (вердикт warning/critical), — в тот же
    // список; в журнал anomalies.jsonl их пишет AppController
    connect(m_llmScheduler, &LlmScheduler::verdictAnomaly,
            this, &MainWindow::onAnomaly);

    connect(m_notifications, &NotificationManager::guiNotification,
            this, &MainWindow::showGuiNotification, Qt::QueuedConnection);
    // Звуковой сигнал — прерогатива оболочки (ядро эмитит только сигнал).
    connect(m_notifications, &NotificationManager::alertSound,
            this, [] { QApplication::beep(); });
    connect(m_notifications, &NotificationManager::notificationStatus, this,
            [this](const QString &channel, const QString &status) {
                statusBar()->showMessage(tr("Оповещение [%1]: %2").arg(channel, status), 5000);
            });

    connect(m_llmScheduler, &LlmScheduler::statusChanged,
            this, &MainWindow::onLlmStatusChanged);

    connect(m_statistics, &StatisticsCollector::statsChanged,
            this, &MainWindow::refreshStats);
    connect(m_statistics, &StatisticsCollector::totalAnomaliesChanged, this,
            [this](qint64 total) {
                m_lblAnomalies->setText(tr("Аномалий: %1").arg(total));
            });

    refreshStats();
    updateCaptureActions();
    loadPacketFilter();
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);

    if (Settings::instance().capture().autoStart)
        toggleCapture();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    m_tabs = new QTabWidget(this);

    // --- Пакеты ---
    auto *packetPage = new QWidget(m_tabs);
    auto *packetLayout = new QVBoxLayout(packetPage);
    packetLayout->setContentsMargins(4, 4, 4, 4);

    // Строка ограничения вывода: показываются только n последних пакетов.
    auto *limitRow = new QHBoxLayout;
    limitRow->addWidget(new QLabel(tr("Показывать последних пакетов:"), packetPage));
    m_packetLimitSpin = new QSpinBox(packetPage);
    m_packetLimitSpin->setRange(50, 100000);
    m_packetLimitSpin->setSingleStep(50);
    m_packetLimitSpin->setValue(Settings::instance().capture().maxTablePackets);
    m_packetLimitSpin->setToolTip(tr("В таблице хранятся только n последних пакетов."));
    limitRow->addWidget(m_packetLimitSpin);
    limitRow->addStretch(1);

    // Фильтр пакетов (отдельный диалог: параметр — действие — значение).
    m_packetFilterBtn = new QPushButton(tr("Фильтр…"), packetPage);
    m_packetFilterBtn->setToolTip(
        tr("Открыть диалог фильтрации: параметр — действие — значение\n"
           "(например, «MAC отправителя | начинается на | bc:24»)."));
    connect(m_packetFilterBtn, &QPushButton::clicked,
            this, &MainWindow::openPacketFilter);
    limitRow->addWidget(m_packetFilterBtn);
    m_packetFilterLabel = new QLabel(packetPage);
    m_packetFilterLabel->setStyleSheet(QStringLiteral("color:#666"));
    limitRow->addWidget(m_packetFilterLabel);
    connect(m_packetLimitSpin, &QSpinBox::valueChanged, this, [this](int value) {
        CaptureSettings capture = Settings::instance().capture();
        capture.maxTablePackets = value;
        Settings::instance().setCapture(capture);
        m_packetModel->setMaxRows(value);   // ужать буфер сразу
    });
    packetLayout->addLayout(limitRow);

    m_packetView = new QTableView(packetPage);
    m_packetProxy = new PacketFilterProxy(this);
    m_packetProxy->setSourceModel(m_packetModel);
    m_packetView->setModel(m_packetProxy);
    m_packetView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_packetView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_packetView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_packetView->setAlternatingRowColors(true);
    m_packetView->verticalHeader()->setVisible(false);
    m_packetView->horizontalHeader()->setStretchLastSection(true);
    m_packetView->horizontalHeader()->setSectionResizeMode(PacketTableModel::ColInfo,
                                                           QHeaderView::Stretch);
    m_packetView->setSortingEnabled(false);
    packetLayout->addWidget(m_packetView);
    connect(m_packetView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onPacketSelectionChanged);
    // Двойной клик по пакету — подробная информация + hex-дамп.
    connect(m_packetView, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index) {
                const Packet *packet = m_packetModel->packetAt(
                    m_packetProxy->mapToSource(index).row());
                if (!packet)
                    return;
                auto *dialog = new PacketDetailsDialog(*packet, this);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->show();
            });
    m_tabs->addTab(packetPage, tr("Пакеты"));

    // --- Аномалии ---
    auto *anomalyPage = new QWidget(m_tabs);
    auto *anomalyLayout = new QVBoxLayout(anomalyPage);
    anomalyLayout->setContentsMargins(4, 4, 4, 4);
    auto *anomalySplitter = new QSplitter(Qt::Vertical, anomalyPage);
    m_anomalyView = new QTableView(anomalySplitter);
    m_anomalyView->setModel(m_anomalyModel);
    m_anomalyView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_anomalyView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_anomalyView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_anomalyView->setAlternatingRowColors(true);
    m_anomalyView->verticalHeader()->setVisible(false);
    m_anomalyView->horizontalHeader()->setStretchLastSection(true);
    m_anomalyView->setColumnHidden(AnomalyTableModel::ColInterface, true);
    m_anomalyDetail = new QPlainTextEdit(anomalySplitter);
    m_anomalyDetail->setReadOnly(true);
    m_anomalyDetail->setPlaceholderText(tr("Выберите аномалию, чтобы увидеть подробности…"));
    anomalySplitter->addWidget(m_anomalyView);
    anomalySplitter->addWidget(m_anomalyDetail);
    anomalySplitter->setStretchFactor(0, 3);
    anomalySplitter->setStretchFactor(1, 1);
    anomalyLayout->addWidget(anomalySplitter);
    connect(m_anomalyView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onAnomalySelectionChanged);
    // Контекстное меню: отметка «решено» оператором
    m_anomalyView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_anomalyView, &QWidget::customContextMenuRequested,
            this, &MainWindow::onAnomalyContextMenu);
    m_tabs->addTab(anomalyPage, tr("Аномалии"));

    // --- Статистика ---
    m_statsWidget = new StatsWidget(m_statistics, m_tabs);
    m_tabs->addTab(m_statsWidget, tr("Статистика"));

    // --- Группировка ---
    m_groupingWidget = new GroupingWidget(m_packetModel, m_tabs);
    m_tabs->addTab(m_groupingWidget, tr("Группировка"));

    // --- Просмотр логов ---
    m_logViewer = new LogViewerWidget(m_tabs);
    m_tabs->addTab(m_logViewer, tr("Просмотр логов"));

    // --- Журнал LLM ---
    auto *journalPage = new QWidget(m_tabs);
    auto *journalLayout = new QVBoxLayout(journalPage);
    journalLayout->setContentsMargins(4, 4, 4, 4);
    auto *journalSplitter = new QSplitter(Qt::Vertical, journalPage);
    m_journalView = new QTableView(journalSplitter);
    m_journalView->setModel(m_journalModel);
    m_journalView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_journalView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_journalView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_journalView->setAlternatingRowColors(true);
    m_journalView->verticalHeader()->setVisible(false);
    m_journalView->horizontalHeader()->setStretchLastSection(true);
    m_journalDetail = new QPlainTextEdit(journalSplitter);
    m_journalDetail->setReadOnly(true);
    m_journalDetail->setPlaceholderText(
        tr("Здесь отображается промпт и полный ответ LLM. "
           "Запросы выполняются по расписанию или кнопкой «Анализ LLM»."));
    journalSplitter->addWidget(m_journalView);
    journalSplitter->addWidget(m_journalDetail);
    journalSplitter->setStretchFactor(0, 2);
    journalSplitter->setStretchFactor(1, 3);
    journalLayout->addWidget(journalSplitter);
    connect(m_journalView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onJournalSelectionChanged);
    m_tabs->addTab(journalPage, tr("Журнал LLM"));

    // --- Настройки ---
    m_settingsWidget = new SettingsWidget(m_interfaces, m_llmScheduler, m_tabs);
    m_tabs->addTab(m_settingsWidget, tr("Настройки"));

    setCentralWidget(m_tabs);
}

void MainWindow::buildToolbar()
{
    auto *toolbar = addToolBar(tr("Основная панель"));
    toolbar->setMovable(false);

    m_startAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Старт захвата"));
    m_stopAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaStop), tr("Стоп"));
    toolbar->addSeparator();
    m_analyzeAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_BrowserReload), tr("Анализ LLM"));
    toolbar->addSeparator();
    auto *clearPackets = toolbar->addAction(tr("Очистить пакеты"));
    auto *clearAnomalies = toolbar->addAction(tr("Очистить аномалии"));
    toolbar->addSeparator();
    auto *helpAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_DialogHelpButton), tr("Справка"));
    helpAction->setShortcut(QKeySequence(Qt::Key_F1));
    helpAction->setShortcutContext(Qt::WindowShortcut);

    connect(m_startAction, &QAction::triggered, this, &MainWindow::toggleCapture);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::toggleCapture);
    connect(m_analyzeAction, &QAction::triggered, this, &MainWindow::onAnalyzeNow);
    connect(clearPackets, &QAction::triggered, this, &MainWindow::clearPacketView);
    connect(clearAnomalies, &QAction::triggered, this, &MainWindow::clearAnomalyView);
    connect(helpAction, &QAction::triggered, this, [this]() {
        HelpDialog::openSmart(this);
    });
}

void MainWindow::buildStatusBar()
{
    m_lblState = new QLabel(this);
    m_lblPackets = new QLabel(this);
    m_lblAnomalies = new QLabel(tr("Аномалий: 0"), this);
    m_lblLlm = new QLabel(tr("LLM: —"), this);
    m_lblLlm->setStyleSheet(QStringLiteral("color:#666"));

    statusBar()->addWidget(m_lblState, 1);
    statusBar()->addPermanentWidget(m_lblPackets);
    statusBar()->addPermanentWidget(m_lblAnomalies);
    statusBar()->addPermanentWidget(m_lblLlm);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QIcon appIcon(QStringLiteral(":/appicon/app.ico"));
    if (appIcon.isNull())
        appIcon = style()->standardIcon(QStyle::SP_ComputerIcon);
    m_tray = new QSystemTrayIcon(appIcon, this);
    m_tray->setToolTip(tr("NetAnomalyAnalyzer"));

    auto *menu = new QMenu(this);
    menu->addAction(tr("Показать окно"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction(m_startAction);
    menu->addAction(m_analyzeAction);
    menu->addSeparator();
    QAction *quitAction = menu->addAction(tr("Выход"));
    connect(quitAction, &QAction::triggered, this, [this] {
        m_exiting = true;               // настоящий выход, а не сворачивание
        if (m_tray)
            m_tray->hide();
        if (m_interfaces && m_interfaces->isCapturing())
            m_interfaces->stopAll();
        close();
    });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger) {
                    if (isVisible()) hide();
                    else { showNormal(); raise(); activateWindow(); }
                } else if (reason == QSystemTrayIcon::DoubleClick) {
                    showNormal();
                    raise();
                    activateWindow();
                }
            });
    m_tray->show();
}

void MainWindow::updateCaptureActions()
{
    const bool capturing = m_interfaces && m_interfaces->isCapturing();
    m_startAction->setEnabled(!capturing);
    m_stopAction->setEnabled(capturing);

    QStringList ifaces;
    if (m_interfaces) {
        const QStringList devices = m_interfaces->activeInterfaces();
        for (const QString &device : devices)
            ifaces.append(CaptureDevice::friendlyName(device));
    }
    m_lblState->setText(capturing
                            ? tr("Захват активен: %1").arg(ifaces.join(QLatin1String(", ")))
                            : tr("Захват остановлен"));
}

void MainWindow::toggleCapture()
{
    if (!m_interfaces)
        return;
    if (m_interfaces->isCapturing()) {
        m_interfaces->stopAll();
        if (m_detector)
            m_detector->resetRules();
        statusBar()->showMessage(tr("Захват остановлен"), 4000);
    } else {
        const int started = m_interfaces->startAll();
        if (started == 0)
            QMessageBox::warning(this, tr("Захват"),
                                 tr("Не удалось запустить ни один интерфейс. "
                                    "Проверьте, что интерфейсы включены в настройках "
                                    "и установлен Npcap."));
        else
            statusBar()->showMessage(tr("Запущено интерфейсов: %1").arg(started), 4000);
    }
    updateCaptureActions();
}

void MainWindow::onPacket(const Packet &packet)
{
    m_pendingPackets.append(packet);
}

void MainWindow::onAnomaly(const Anomaly &anomaly)
{
    m_anomalyModel->addAnomaly(anomaly);
    appendAnomalyDetail(anomaly);
    const QWidget *anomalyPage = m_anomalyView->parentWidget()
            ? m_anomalyView->parentWidget()->parentWidget() : nullptr;
    if (anomalyPage && m_tabs->currentWidget() == anomalyPage)
        m_anomalyView->scrollToBottom();
}

void MainWindow::appendAnomalyDetail(const Anomaly &anomaly)
{
    const QString text = QStringLiteral("%1  [%2] %3 — %4\n"
                                        "Обнаружение: %5\n"
                                        "Источник: %6 → %7 (%8)\n"
                                        "Интерфейс: %9, пакетов: %10\n%11\n\n")
                             .arg(anomaly.timestamp().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss.zzz")),
                                  anomaly.severityName(),
                                  anomaly.ruleName(),
                                  anomaly.title(),
                                  anomaly.detector().isEmpty()
                                      ? Anomaly::defaultDetectorName()
                                      : anomaly.detector(),
                                  anomaly.sourceIp(),
                                  anomaly.destinationIp(),
                                  anomaly.sample(),
                                  CaptureDevice::friendlyName(anomaly.interfaceName()))
                             .arg(anomaly.packetCount())
                             .arg(anomaly.description());
    m_anomalyDetail->appendPlainText(text);
}

void MainWindow::preloadAnomalies(const QVector<QJsonObject> &records)
{
    for (const QJsonObject &record : records) {
        const Anomaly anomaly = Anomaly::fromJson(record);
        const QString status =
            m_logWriter ? m_logWriter->anomalyStatus(AnomalyStatusStore::keyFor(record))
                        : QLatin1String(AnomalyStatusStore::kDetected);
        m_anomalyModel->addAnomaly(anomaly, status);
    }
    if (!records.isEmpty()) {
        m_anomalyView->resizeColumnsToContents();
        statusBar()->showMessage(
            tr("Загружено записей журнала аномалий: %1").arg(records.size()), 5000);
    }
}

void MainWindow::onAnomalyContextMenu(const QPoint &pos)
{
    const int row = m_anomalyView->rowAt(pos.y());
    const Anomaly *anomaly = m_anomalyModel->anomalyAt(row);
    if (!anomaly || !m_logWriter)
        return;

    const QString key = AnomalyStatusStore::keyFor(
        anomaly->id(), anomaly->timestamp().toString(Qt::ISODate));
    const bool resolved =
        m_anomalyModel->statusAt(row) == QLatin1String(AnomalyStatusStore::kResolved);

    QMenu menu(this);
    QAction *toggle = menu.addAction(resolved
                                         ? tr("Снять отметку «решено»")
                                         : tr("Отметить решённой"));
    if (menu.exec(m_anomalyView->viewport()->mapToGlobal(pos)) == toggle) {
        m_logWriter->setAnomalyResolved(key, !resolved);
        m_anomalyModel->setStatus(
            row, !resolved ? QLatin1String(AnomalyStatusStore::kResolved)
                           : QLatin1String(AnomalyStatusStore::kDetected));
        statusBar()->showMessage(!resolved ? tr("Аномалия отмечена решённой")
                                           : tr("Отметка «решено» снята"), 4000);
    }
}

void MainWindow::showGuiNotification(const Anomaly &anomaly)
{
    const NotificationSettings &settings = Settings::instance().notifications();
    if (!settings.guiPopups || !m_tray)
        return;

    m_tray->showMessage(tr("Аномалия: %1").arg(anomaly.severityName()),
                        anomaly.notificationText(),
                        anomaly.severity() == Anomaly::Severity::Critical
                            ? QSystemTrayIcon::Critical
                            : QSystemTrayIcon::Warning,
                        8000);
}

void MainWindow::refreshStats()
{
    if (!m_statistics)
        return;

    double pps = 0.0;
    const auto &stats = m_statistics->interfaceStats();
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it)
        pps += it.value().pps;

    m_lblPackets->setText(tr("Пакетов: %1 (%2/с)")
                              .arg(m_statistics->totalPackets())
                              .arg(pps, 0, 'f', 1));

    const QModelIndex packetIndex = m_packetView->currentIndex();
    if (m_tabs->currentWidget() == m_packetView->parentWidget() && !packetIndex.isValid())
        m_packetView->scrollToBottom();

    if (m_tabs->currentWidget() == m_statsWidget)
        m_statsWidget->refresh();
}

void MainWindow::onCurrentTabChanged(int index)
{
    if (m_tabs->widget(index) == m_statsWidget)
        m_statsWidget->refresh();
    else if (m_tabs->widget(index) == m_logViewer)
        m_logViewer->refreshFiles();   // подхватить новые записи журналов
    else if (m_tabs->widget(index) == m_groupingWidget)
        m_groupingWidget->refresh();   // пересчитать группы по свежему буферу
}

void MainWindow::onLlmStatusChanged(const QString &status)
{
    m_lblLlm->setText(status);
}

void MainWindow::onAnalyzeNow()
{
    if (m_llmScheduler)
        m_llmScheduler->requestManualAnalysis();
}

void MainWindow::onCaptureError(const QString &device, const QString &message)
{
    statusBar()->showMessage(tr("Ошибка [%1]: %2").arg(device, message), 8000);
}

void MainWindow::openPacketFilter()
{
    FilterDialog dialog(m_packetProxy->conditions(),
                        FilterCondition::packetFields(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_packetProxy->setConditions(dialog.conditions());
    savePacketFilter();

    QStringList parts;
    for (const FilterCondition &condition : m_packetProxy->conditions())
        parts << condition.describe();
    m_packetFilterLabel->setText(parts.join(QStringLiteral("  И  ")));
    statusBar()->showMessage(
        parts.isEmpty() ? tr("Фильтр пакетов сброшен")
                        : tr("Фильтр пакетов: %1").arg(parts.join(QStringLiteral(", "))),
        5000);
}

void MainWindow::loadPacketFilter()
{
    QSettings s = Settings::openSettings();
    const int count = s.beginReadArray(QStringLiteral("filters/packets"));
    QVector<FilterCondition> conditions;
    conditions.reserve(count);
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        FilterCondition condition;
        condition.field = static_cast<FilterCondition::Field>(
            s.value(QStringLiteral("field"), 0).toInt());
        condition.op = static_cast<FilterCondition::Op>(
            s.value(QStringLiteral("op"), 0).toInt());
        condition.value = s.value(QStringLiteral("value")).toString();
        if (!condition.value.isEmpty())
            conditions.append(condition);
    }
    s.endArray();
    if (conditions.isEmpty())
        return;
    m_packetProxy->setConditions(conditions);
    QStringList parts;
    for (const FilterCondition &condition : conditions)
        parts << condition.describe();
    m_packetFilterLabel->setText(parts.join(QStringLiteral("  И  ")));
}

void MainWindow::savePacketFilter()
{
    QSettings s = Settings::openSettings();
    s.remove(QStringLiteral("filters/packets"));
    const QVector<FilterCondition> conditions = m_packetProxy->conditions();
    if (conditions.isEmpty())
        return;
    s.beginWriteArray(QStringLiteral("filters/packets"), conditions.size());
    for (int i = 0; i < conditions.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("field"), static_cast<int>(conditions.at(i).field));
        s.setValue(QStringLiteral("op"), static_cast<int>(conditions.at(i).op));
        s.setValue(QStringLiteral("value"), conditions.at(i).value);
    }
    s.endArray();
}

void MainWindow::onPacketSelectionChanged()
{
    const Packet *packet = m_packetModel->packetAt(
        m_packetProxy->mapToSource(m_packetView->currentIndex()).row());
    if (!packet)
        return;
    statusBar()->showMessage(packet->summary() +
                                 (packet->payload().isEmpty()
                                      ? QString()
                                      : QStringLiteral("  payload: «%1»")
                                            .arg(packet->payloadPreview(80))),
                              6000);
}

void MainWindow::onAnomalySelectionChanged()
{
    const QModelIndex index = m_anomalyView->currentIndex();
    const Anomaly *anomaly = m_anomalyModel->anomalyAt(index.row());
    if (!anomaly)
        return;

    QString statusText =
        AnomalyTableModel::statusDisplayName(m_anomalyModel->statusAt(index.row()));
    if (m_anomalyModel->statusAt(index.row())
        == QLatin1String(AnomalyStatusStore::kResolved))
        statusText += tr(" (отмечено вручную)");

    m_anomalyDetail->setPlainText(
        QStringLiteral("Аномалия #%1\n"
                       "Время: %2\n"
                       "Уровень: %3\n"
                       "Статус: %4\n"
                       "Правило: %5\n"
                       "Интерфейс: %6\n"
                       "Источник: %7:%8\n"
                       "Назначение: %9:%10\n"
                       "Пакетов: %11\n\n"
                       "%12\n\n"
                       "Пример: %13")
            .arg(anomaly->id())
            .arg(anomaly->timestamp().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss.zzz")))
            .arg(anomaly->severityName())
            .arg(statusText)
            .arg(anomaly->ruleName())
            .arg(CaptureDevice::friendlyName(anomaly->interfaceName()))
            .arg(anomaly->sourceIp())
            .arg(anomaly->sourcePort())
            .arg(anomaly->destinationIp())
            .arg(anomaly->destinationPort())
            .arg(anomaly->packetCount())
            .arg(anomaly->description())
            .arg(anomaly->sample()));
}

void MainWindow::onJournalSelectionChanged()
{
    const QModelIndex index = m_journalView->currentIndex();
    const LlmJournalEntry *entry = m_journalModel->entryAt(index.row());
    if (!entry)
        return;

    QString text = QStringLiteral("Обращение #%1\n"
                                  "Время: %2\n"
                                  "Источник: %3\n"
                                  "Статус: %4\n"
                                  "Вердикт LLM: %5\n"
                                  "Модель: %6\n"
                                  "Длительность: %7 с\n"
                                  "Аномалий в контексте: %8\n")
                       .arg(entry->id)
                       .arg(entry->startTime.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss.zzz")))
                       .arg(LlmJournalEntry::sourceName(entry->source))
                       .arg(LlmJournalEntry::statusName(entry->status))
                       .arg(LlmJournalEntry::verdictName(entry->verdictStatus))
                       .arg(entry->model)
                       .arg(entry->durationSec())
                       .arg(entry->anomaliesInPrompt);
    if (!entry->logPeriodFrom.isEmpty())
        text += QStringLiteral("Период журнала событий: с %1\n")
                    .arg(entry->logPeriodFrom);
    text += QStringLiteral("\n=== ПРОМПТ ===\n%1\n\n")
                .arg(entry->prompt.isEmpty() ? entry->promptPreview : entry->prompt);

    if (!entry->verdictStatus.isEmpty()) {
        text += QStringLiteral("=== ВЕРДИКТ LLM (%1) ===\n")
                    .arg(LlmJournalEntry::verdictName(entry->verdictStatus));
        const QString details = QString::fromUtf8(
            QJsonDocument(entry->verdictData).toJson(QJsonDocument::Indented));
        text += details.isEmpty() ? tr("(нет структурированных данных)")
                                  : details;
        text += QLatin1Char('\n');
    }

    if (entry->status == LlmJournalEntry::Status::Error)
        text += QStringLiteral("=== ОШИБКА ===\n%1").arg(entry->error);
    else
        text += QStringLiteral("=== ОТВЕТ LLM ===\n%1")
                    .arg(entry->response.isEmpty() ? tr("…") : entry->response);

    m_journalDetail->setPlainText(text);
}

void MainWindow::clearPacketView()
{
    m_packetModel->clear();
    m_pendingPackets.clear();
}

void MainWindow::clearAnomalyView()
{
    m_anomalyModel->clear();
    m_anomalyDetail->clear();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_exiting && m_tray && Settings::instance().notifications().minimizeToTray) {
        // Фоновый режим: прячем окно в трей, приложение продолжает работать
        // (захват, детект, оповещения и LLM-расписание активны).
        event->ignore();
        hide();
        if (!m_trayHintShown) {
            m_tray->showMessage(
                tr("NetAnomalyAnalyzer свёрнут в трей"),
                tr("Захват продолжает работать в фоне. Открыть окно — двойной клик "
                   "по иконке, выход — пункт «Выход» в меню иконки."),
                QSystemTrayIcon::Information, 5000);
            m_trayHintShown = true;
        }
        return;
    }

    if (m_interfaces && m_interfaces->isCapturing())
        m_interfaces->stopAll();
    QMainWindow::closeEvent(event);
}
