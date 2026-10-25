#include "StatsWidget.h"
#include "ProtocolBarWidget.h"
#include "TimeSeriesWidget.h"
#include "../core/CaptureDevice.h"
#include "../stats/StatisticsCollector.h"

#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString formatBytes(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 Б").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 КБ").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 МБ").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 ГБ").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}
} // namespace

StatsWidget::StatsWidget(StatisticsCollector *statistics, QWidget *parent)
    : QWidget(parent)
    , m_statistics(statistics)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_totalsLabel = new QLabel(this);
    m_totalsLabel->setWordWrap(true);
    QFont bold = m_totalsLabel->font();
    bold.setBold(true);
    m_totalsLabel->setFont(bold);
    layout->addWidget(m_totalsLabel);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);

    // Сводная таблица интерфейсов
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        tr("Интерфейс"), tr("Пакетов"), tr("Байт"), tr("Пак/с"),
        tr("Кбит/с"), tr("Аномалий"), tr("Доля")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    grid->addWidget(m_table, 0, 0, 3, 1);

    m_graph = new TimeSeriesWidget(this);
    m_graph->setTitle(tr("Загруженность: пакетов в секунду по времени (последние 5 минут)"));
    m_graph->setYLabel(tr("пак/с"));
    grid->addWidget(m_graph, 0, 1);

    m_trafficGraph = new TimeSeriesWidget(this);
    m_trafficGraph->setTitle(tr("Сетевой трафик по времени (последние 5 минут)"));
    m_trafficGraph->setYLabel(tr("Кбит/с"));
    grid->addWidget(m_trafficGraph, 1, 1);

    m_protocols = new ProtocolBarWidget(this);
    m_protocols->setTitle(tr("Распределение по протоколам"));
    grid->addWidget(m_protocols, 2, 1);

    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(1, 3);
    grid->setRowStretch(0, 3);
    grid->setRowStretch(1, 3);
    grid->setRowStretch(2, 3);
    layout->addLayout(grid, 1);

    refresh();
}

void StatsWidget::refresh()
{
    if (!m_statistics)
        return;

    m_totalsLabel->setText(
        tr("Всего: %1 пакетов · %2 · аномалий: %3 · интерфейсов: %4")
            .arg(m_statistics->totalPackets())
            .arg(formatBytes(m_statistics->totalBytes()))
            .arg(m_statistics->totalAnomalies())
            .arg(m_statistics->interfaceStats().size()));

    const auto &stats = m_statistics->interfaceStats();
    m_table->setRowCount(stats.size());

    const qint64 totalPackets = qMax<qint64>(1, m_statistics->totalPackets());
    QMap<QString, QVector<double>> series;
    QMap<QString, QVector<double>> trafficSeries;

    int row = 0;
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it, ++row) {
        const StatisticsCollector::InterfaceStats &s = it.value();
        const QString displayName = CaptureDevice::friendlyName(s.name);
        m_table->setItem(row, 0, new QTableWidgetItem(displayName));
        m_table->item(row, 0)->setToolTip(s.name);
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(s.packets)));
        m_table->setItem(row, 2, new QTableWidgetItem(formatBytes(s.bytes)));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::number(s.pps, 'f', 1)));
        m_table->setItem(row, 4, new QTableWidgetItem(QString::number(s.bps * 8 / 1000.0, 'f', 1)));
        m_table->setItem(row, 5, new QTableWidgetItem(QString::number(static_cast<qint64>(s.totalAnomalies))));
        auto *share = new QTableWidgetItem(
            QStringLiteral("%1 %").arg(double(s.packets) * 100.0 / totalPackets, 0, 'f', 1));
        share->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 6, share);

        series.insert(displayName, s.history);
        trafficSeries.insert(displayName, s.historyKbps);
    }

    m_graph->setSeries(series);
    m_trafficGraph->setSeries(trafficSeries);
    m_protocols->setCounts(m_statistics->protocolCounts());
}
