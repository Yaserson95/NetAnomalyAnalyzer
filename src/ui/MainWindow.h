#pragma once

#include "../anomaly/Anomaly.h"
#include "../core/Packet.h"

#include <QJsonObject>
#include <QMainWindow>
#include <QVector>

class PacketTableModel;
class AnomalyTableModel;
class LlmJournalModel;
class StatsWidget;
class GroupingWidget;
class SettingsWidget;
class LogViewerWidget;
class InterfaceManager;
class StatisticsCollector;
class NotificationManager;
class LlmScheduler;
class AnomalyDetector;
class LogWriter;
class QTabWidget;
class QTableView;
class QLabel;
class QSystemTrayIcon;
class QAction;
class QPlainTextEdit;
class QTimer;
class QSplitter;
class QSpinBox;
class QPushButton;
class QPoint;
class PacketFilterProxy;

/**
 * @brief Главное окно: поток пакетов, журнал аномалий, статистика,
 * просмотр логов, журнал обращений к LLM и настройки.
 */
class AppController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    /**
     * Создать главное окно поверх готового ядра (AppController) —
     * единая точка сборки GUI-оболочки для основного executable.
     */
    static MainWindow *create(AppController *controller, QWidget *parent = nullptr);

    MainWindow(InterfaceManager *interfaces,
               AnomalyDetector *detector,
               StatisticsCollector *statistics,
               NotificationManager *notifications,
               LlmScheduler *llmScheduler,
               LogWriter *logWriter,
               QWidget *parent = nullptr);
    ~MainWindow() override;

    /** Подгрузить в журнал аномалий сохранённую историю (JSON-записи
     *  журнала anomalies.jsonl со статусами из anomaly-status.json). */
    void preloadAnomalies(const QVector<QJsonObject> &records);

protected:
    void closeEvent(QCloseEvent *event) override;

public slots:
    void onPacket(const Packet &packet);
    void onAnomaly(const Anomaly &anomaly);

private slots:
    void toggleCapture();
    void refreshStats();
    void onLlmStatusChanged(const QString &status);
    void showGuiNotification(const Anomaly &anomaly);
    void onCurrentTabChanged(int index);
    void onPacketSelectionChanged();
    void onAnomalySelectionChanged();
    void onJournalSelectionChanged();
    void onAnomalyContextMenu(const QPoint &pos);
    void clearPacketView();
    void clearAnomalyView();
    void onCaptureError(const QString &device, const QString &message);
    void onAnalyzeNow();
    void openPacketFilter();

private:
    void buildUi();
    void buildToolbar();
    void buildStatusBar();
    void buildTray();
    void updateCaptureActions();
    void appendAnomalyDetail(const Anomaly &anomaly);
    /** Сохранить/восстановить фильтр пакетов в настройках (между запусками). */
    void loadPacketFilter();
    void savePacketFilter();

    InterfaceManager   *m_interfaces;
    AnomalyDetector    *m_detector;
    StatisticsCollector *m_statistics;
    NotificationManager *m_notifications;
    LlmScheduler       *m_llmScheduler;
    LogWriter          *m_logWriter = nullptr;   ///< статусы «решено» по аномалиям

    PacketTableModel   *m_packetModel = nullptr;
    PacketFilterProxy  *m_packetProxy = nullptr;
    GroupingWidget     *m_groupingWidget = nullptr;
    AnomalyTableModel  *m_anomalyModel = nullptr;
    LlmJournalModel    *m_journalModel = nullptr;
    StatsWidget        *m_statsWidget = nullptr;
    SettingsWidget     *m_settingsWidget = nullptr;
    LogViewerWidget    *m_logViewer = nullptr;

    QTableView  *m_packetView = nullptr;
    QTableView  *m_anomalyView = nullptr;
    QTableView  *m_journalView = nullptr;
    QPlainTextEdit *m_anomalyDetail = nullptr;
    QPlainTextEdit *m_journalDetail = nullptr;
    QTabWidget  *m_tabs = nullptr;
    QSpinBox    *m_packetLimitSpin = nullptr;
    QPushButton *m_packetFilterBtn = nullptr;
    QLabel      *m_packetFilterLabel = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_analyzeAction = nullptr;
    bool m_exiting = false;        ///< настоящий выход (не сворачивание в трей)
    bool m_trayHintShown = false;

    QLabel *m_lblState = nullptr;
    QLabel *m_lblPackets = nullptr;
    QLabel *m_lblAnomalies = nullptr;
    QLabel *m_lblLlm = nullptr;

    QVector<Packet> m_pendingPackets;   ///< батчинг обновлений таблицы
    QTimer *m_flushTimer = nullptr;
};
