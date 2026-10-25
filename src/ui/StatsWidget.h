#pragma once

#include <QWidget>

class StatisticsCollector;
class TimeSeriesWidget;
class ProtocolBarWidget;
class QTableWidget;
class QLabel;

/**
 * @brief Вкладка «Статистика»: сводная информация по интерфейсам,
 * графики загруженности (пакеты во времени) и распределение протоколов.
 */
class StatsWidget : public QWidget {
    Q_OBJECT
public:
    explicit StatsWidget(StatisticsCollector *statistics, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    StatisticsCollector *m_statistics;
    QLabel              *m_totalsLabel = nullptr;
    QTableWidget        *m_table = nullptr;
    TimeSeriesWidget    *m_graph = nullptr;
    TimeSeriesWidget    *m_trafficGraph = nullptr;
    ProtocolBarWidget   *m_protocols = nullptr;
};
