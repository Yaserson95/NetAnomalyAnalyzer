#include "ProtocolBarWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QFontMetrics>

#include <algorithm>

ProtocolBarWidget::ProtocolBarWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
}

void ProtocolBarWidget::setCounts(const QHash<QString, qint64> &counts)
{
    m_counts = counts;
    update();
}

void ProtocolBarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(250, 250, 252));

    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    const QFontMetrics fm(font);

    // --- Заголовок ---------------------------------------------------------
    painter.setPen(QColor(60, 60, 60));
    painter.drawText(QRect(8, 6, width() - 16, 22), Qt::AlignLeft | Qt::AlignVCenter,
                     m_title);

    // Сортировка по убыванию
    QList<QPair<QString, qint64>> items;
    for (auto it = m_counts.constBegin(); it != m_counts.constEnd(); ++it)
        items.append(qMakePair(it.key(), it.value()));
    std::sort(items.begin(), items.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    if (items.isEmpty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect().adjusted(10, 30, -10, -10),
                         Qt::AlignCenter, tr("Нет данных — запустите захват трафика"));
        return;
    }

    qint64 max = 1;
    for (const auto &item : items)
        max = qMax(max, item.second);

    static const QColor colors[] = {
        QColor(41, 128, 185), QColor(39, 174, 96), QColor(243, 156, 18),
        QColor(231, 76, 60), QColor(142, 68, 173), QColor(22, 160, 133),
        QColor(44, 62, 80), QColor(127, 140, 141)
    };
    constexpr int nColors = int(sizeof(colors) / sizeof(colors[0]));

    // --- Расчёт компоновки -------------------------------------------------
    const int top = 34;                 // под заголовком
    const int bottom = height() - 8;    // нижний отступ
    const int areaH = bottom - top;

    // Высота строки: вписываем все строки, но держим разумные границы.
    int rowHeight = items.isEmpty() ? 16 : areaH / items.size();
    rowHeight = qBound(16, rowHeight, 28);

    // Колонка подписей — по самому длинному имени (с потолком 40% ширины).
    int nameW = 60;
    for (const auto &item : items)
        nameW = qMax(nameW, fm.horizontalAdvance(item.first) + 12);
    nameW = qMin(nameW, qMax(60, int(width() * 0.35)));

    // Колонка значений — по самому широкому числу, всегда справа у края.
    int valueW = 40;
    for (const auto &item : items)
        valueW = qMax(valueW, fm.horizontalAdvance(QString::number(item.second)) + 12);
    valueW = qMin(valueW, qMax(40, int(width() * 0.25)));

    // Полосы живут в фиксированной области между колонками — ничего не
    // наезжает на подписи, даже при узком виджете.
    const int barLeft = nameW + 10;
    const int barRight = width() - valueW - 10;
    const int barAreaW = qMax(10, barRight - barLeft);

    // Сколько строк реально помещается (с учётом возможной строки «ещё»).
    int drawable = qMax(1, areaH / rowHeight);
    if (drawable < items.size() && areaH - drawable * rowHeight < 14 && drawable > 1)
        --drawable;  // оставить место под «… и ещё N»

    const int shown = qMin(items.size(), drawable);
    const int hidden = items.size() - shown;

    // --- Отрисовка строк ---------------------------------------------------
    int y = top;
    for (int i = 0; i < shown; ++i, y += rowHeight) {
        const QString name = fm.elidedText(items.at(i).first, Qt::ElideRight, nameW - 6);
        const qint64 value = items.at(i).second;
        const int w = int(double(value) / max * barAreaW);
        const int barH = rowHeight - 8;

        painter.setPen(QColor(60, 60, 60));
        painter.drawText(QRect(6, y, nameW - 6, rowHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, name);

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors[i % nColors]);
        painter.drawRoundedRect(QRect(barLeft, y + 4, w, barH), 3, 3);

        // Значение — строго в своей колонке, у правого края.
        painter.setPen(QColor(60, 60, 60));
        painter.drawText(QRect(barRight + 6, y, valueW - 6, rowHeight),
                         Qt::AlignRight | Qt::AlignVCenter, QString::number(value));
    }

    if (hidden > 0 && y + 14 <= height()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(QRect(8, y, width() - 16, 14),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         tr("… и ещё %1").arg(hidden));
    }
}
