#include "TimeSeriesWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QPolygon>

namespace {
QColor seriesColor(int index)
{
    static const QColor palette[] = {
        QColor(41, 128, 185), QColor(231, 76, 60), QColor(39, 174, 96),
        QColor(243, 156, 18), QColor(142, 68, 173), QColor(22, 160, 133),
        QColor(192, 57, 43),  QColor(44, 62, 80)
    };
    return palette[index % int(sizeof(palette) / sizeof(palette[0]))];
}
} // namespace

TimeSeriesWidget::TimeSeriesWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(200);
}

void TimeSeriesWidget::setSeries(const QMap<QString, QVector<double>> &series)
{
    m_series = series;
    update();
}

void TimeSeriesWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(250, 250, 252));

    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    const QFontMetrics fm(font);

    // --- Легенда: раскладка заранее, чтобы освободить место под неё --------
    struct LegendEntry { QString name; double last; QColor color; };
    QVector<LegendEntry> legend;
    int colorIndex = 0;
    for (auto it = m_series.constBegin(); it != m_series.constEnd(); ++it, ++colorIndex) {
        if (it.value().isEmpty())
            continue;
        legend.append({it.key(), it.value().last(), seriesColor(colorIndex)});
    }

    const int left = 56, right = 16, top = 34;
    const int legendRowH = 17;
    int legendRows = 0;
    if (!legend.isEmpty()) {
        int x = 0;
        legendRows = 1;
        for (const LegendEntry &entry : legend) {
            const QString text = QStringLiteral("%1 — %2")
                                     .arg(entry.name)
                                     .arg(entry.last, 0, 'f', 1);
            const int w = fm.horizontalAdvance(text) + 26; // + квадратик и отступ
            if (x > 0 && x + w > width() - right - left) {
                ++legendRows;
                x = 0;
            }
            x += w;
        }
    }

    const int bottom = 8 + legendRows * legendRowH;
    const QRect plot(left, top, width() - left - right, height() - top - bottom);
    if (plot.width() <= 10 || plot.height() <= 10)
        return;

    // --- Заголовок: слева название, справа единица измерения --------------
    if (!m_title.isEmpty()) {
        QString title = m_title;
        int titleW = fm.horizontalAdvance(title);
        const int labelW = m_yLabel.isEmpty() ? 0 : fm.horizontalAdvance(m_yLabel) + 12;
        const int availW = width() - 16 - labelW;
        if (titleW > availW)
            title = fm.elidedText(title, Qt::ElideRight, availW);
        painter.setPen(QColor(60, 60, 60));
        painter.drawText(QRect(8, 6, width() - 16 - labelW, 22),
                         Qt::AlignLeft | Qt::AlignVCenter, title);
        if (labelW > 0)
            painter.drawText(QRect(width() - labelW - 8, 6, labelW, 22),
                             Qt::AlignRight | Qt::AlignVCenter, m_yLabel);
    }

    double maxValue = 1.0;
    int maxLength = 0;
    for (auto it = m_series.constBegin(); it != m_series.constEnd(); ++it) {
        maxLength = qMax(maxLength, it.value().size());
        for (double value : it.value())
            maxValue = qMax(maxValue, value);
    }
    maxValue *= 1.15;
    if (maxLength < 2)
        maxLength = 2;

    if (legend.isEmpty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(plot, Qt::AlignCenter,
                         tr("Нет данных — запустите захват трафика"));
        return;
    }

    // --- Сетка + подписи оси Y --------------------------------------------
    painter.setPen(QPen(QColor(215, 215, 220), 1));
    const int gridLines = 5;
    for (int i = 0; i <= gridLines; ++i) {
        const double value = maxValue * i / gridLines;
        const int y = plot.bottom() - int(plot.height() * i / gridLines);
        painter.drawLine(plot.left(), y, plot.right(), y);
        painter.setPen(QColor(110, 110, 110));
        painter.drawText(QRect(0, y - 9, plot.left() - 6, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(value, 'f', value < 10 ? 1 : 0));
        painter.setPen(QPen(QColor(215, 215, 220), 1));
    }

    // Оси
    painter.setPen(QPen(QColor(140, 140, 145), 1));
    painter.drawRect(plot);

    // --- Ряды --------------------------------------------------------------
    colorIndex = 0;
    painter.setClipRect(plot.adjusted(0, 0, -1, -1));
    for (auto it = m_series.constBegin(); it != m_series.constEnd(); ++it, ++colorIndex) {
        const QVector<double> &values = it.value();
        if (values.isEmpty())
            continue;

        QPolygonF line;
        for (int i = 0; i < values.size(); ++i) {
            const double x = plot.left() + double(i) * plot.width() / (maxLength - 1);
            const double y = plot.bottom() - (values.at(i) / maxValue) * plot.height();
            line << QPointF(x, y);
        }

        const QColor color = seriesColor(colorIndex);
        // заливка под линией
        QPolygonF area = line;
        area << QPointF(plot.left() + double(values.size() - 1) * plot.width() / (maxLength - 1),
                        plot.bottom())
             << QPointF(plot.left(), plot.bottom());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 40));
        painter.drawPolygon(area);

        painter.setPen(QPen(color, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(line);
    }
    painter.setClipping(false);

    // --- Легенда под графиком (не перекрывает линии) ------------------------
    int legendY = plot.bottom() + 4;
    int x = 8;
    for (const LegendEntry &entry : legend) {
        const QString text = QStringLiteral("%1 — %2")
                                 .arg(entry.name)
                                 .arg(entry.last, 0, 'f', 1);
        const int textW = fm.horizontalAdvance(text);
        const int w = textW + 26;
        if (x > 8 && x + w > width() - 8) {
            x = 8;
            legendY += legendRowH;
        }
        painter.setBrush(entry.color);
        painter.setPen(Qt::NoPen);
        painter.drawRect(x, legendY + 3, 10, 10);
        painter.setPen(QColor(60, 60, 60));
        painter.drawText(QRect(x + 16, legendY, textW + 4, legendRowH),
                         Qt::AlignLeft | Qt::AlignVCenter, text);
        x += w;
    }
}
