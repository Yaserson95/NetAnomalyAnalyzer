#include "TimelineWidget.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include <algorithm>

namespace {
/** «Красивый» шаг шкалы времени (мс) для данного периода. */
qint64 niceStep(qint64 spanMs)
{
    const qint64 candidates[] = {
        1000, 5000, 10000, 30000, 60000,                 // 1с, 5с, 10с, 30с, 1м
        300000, 600000, 900000, 1800000,                  // 5м, 10м, 15м, 30м
        3600000, 7200000, 10800000, 21600000,             // 1ч, 2ч, 3ч, 6ч
        43200000, 86400000, 604800000                     // 12ч, 1д, 7д
    };
    for (qint64 step : candidates) {
        if (spanMs / step <= 12)
            return step;
    }
    return candidates[std::size(candidates) - 1];
}

QString formatTick(qint64 ms, qint64 step)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    if (step >= 86400000)
        return dt.toString(QStringLiteral("dd.MM"));
    if (step >= 60000)
        return dt.toString(QStringLiteral("HH:mm"));
    return dt.toString(QStringLiteral("HH:mm:ss"));
}
} // namespace

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(false);
}

void TimelineWidget::setEvents(QVector<Event> events)
{
    std::stable_sort(events.begin(), events.end(),
                     [](const Event &a, const Event &b) { return a.timeMs < b.timeMs; });
    m_events = std::move(events);

    if (m_events.isEmpty()) {
        m_minMs = m_maxMs = 0;
        m_selStartMs = m_selEndMs = 0;
    } else {
        // Небольшой запас по краям, чтобы крайние события были видны.
        const qint64 margin = qMax<qint64>(1000, (m_events.last().timeMs - m_events.first().timeMs) / 100);
        m_minMs = m_events.first().timeMs - margin;
        m_maxMs = m_events.last().timeMs + margin;
        m_selStartMs = m_minMs;
        m_selEndMs = m_maxMs;
    }
    update();
}

void TimelineWidget::selectFullRange()
{
    if (!rangeAvailable())
        return;
    setSelection(m_minMs, m_maxMs);
    emit selectionChanged(m_selStartMs, m_selEndMs);
}

int TimelineWidget::trackTop() const
{
    return kMargin;
}

int TimelineWidget::trackBottom() const
{
    return height() - kAxisHeight - 2;
}

int TimelineWidget::xOfMs(qint64 ms) const
{
    if (!rangeAvailable())
        return kMargin;
    const qreal w = width() - 2 * kMargin;
    const qreal t = qreal(ms - m_minMs) / qreal(m_maxMs - m_minMs);
    return kMargin + qRound(t * w);
}

qint64 TimelineWidget::msOfX(int x) const
{
    if (!rangeAvailable())
        return 0;
    const qreal w = width() - 2 * kMargin;
    qreal t = qreal(x - kMargin) / w;
    t = qBound(0.0, t, 1.0);
    return m_minMs + qint64(t * double(m_maxMs - m_minMs));
}

void TimelineWidget::ensureVisibleRange(qint64 &ms) const
{
    ms = qBound(m_minMs, ms, m_maxMs);
}

void TimelineWidget::setSelection(qint64 startMs, qint64 endMs)
{
    ensureVisibleRange(startMs);
    ensureVisibleRange(endMs);
    if (startMs > endMs)
        std::swap(startMs, endMs);
    if (endMs - startMs < kMinSelectionMs) {
        // Не даём выделению схлопнуться: расширяем в сторону большего края.
        if (startMs + kMinSelectionMs <= m_maxMs)
            endMs = startMs + kMinSelectionMs;
        else
            startMs = endMs - kMinSelectionMs;
    }
    m_selStartMs = startMs;
    m_selEndMs = endMs;
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect track(kMargin, trackTop(), width() - 2 * kMargin,
                      trackBottom() - trackTop());

    // Фон шкалы
    p.fillRect(track, QColor(245, 247, 250));
    p.setPen(QColor(205, 211, 219));
    p.drawRect(track);

    if (rangeAvailable()) {
        // --- Гистограмма событий: агрегация по пиксельным столбцам ---
        const int w = track.width();
        QVector<qint64> counts(w, 0);
        QVector<QColor> topColors(w);
        QVector<QVector<QPair<QColor, int>>> colorVotes(w);

        for (const Event &e : std::as_const(m_events)) {
            const int idx = qBound(0, xOfMs(e.timeMs) - track.left(), w - 1);
            ++counts[idx];
            auto &votes = colorVotes[idx];
            bool found = false;
            for (auto &v : votes) {
                if (v.first == e.color) {
                    ++v.second;
                    found = true;
                    break;
                }
            }
            if (!found)
                votes.append({e.color, 1});
        }
        qint64 maxCount = 1;
        for (qint64 c : counts)
            maxCount = qMax(maxCount, c);

        for (int i = 0; i < w; ++i) {
            if (counts[i] == 0)
                continue;
            // Цвет наиболее частого события в столбце
            QColor color(52, 120, 198);
            int best = 0;
            for (const auto &v : std::as_const(colorVotes[i])) {
                if (v.second > best) {
                    best = v.second;
                    color = v.first;
                }
            }
            // Высота — логарифмическая шкала, чтобы редкие события были видны
            const qreal norm = std::log1p(double(counts[i])) / std::log1p(double(maxCount));
            const int barH = qMax(2, qRound(norm * (track.height() - 6)));
            p.fillRect(track.left() + i, track.bottom() - barH, 1, barH, color);
        }

        // --- Выделенный диапазон ---
        const int xs = xOfMs(m_selStartMs);
        const int xe = xOfMs(m_selEndMs);
        QRect sel(xs, track.top(), qMax(1, xe - xs), track.height());
        p.fillRect(sel, QColor(33, 150, 243, 45));
        p.setPen(QColor(33, 150, 243));
        p.drawRect(sel);

        // Ручки
        p.fillRect(QRect(xs - 2, track.top(), 5, track.height()), QColor(33, 150, 243));
        p.fillRect(QRect(xe - 2, track.top(), 5, track.height()), QColor(33, 150, 243));

        // --- Подписи оси времени ---
        p.setPen(QColor(90, 98, 108));
        const qint64 step = niceStep(m_maxMs - m_minMs);
        // Первый тик — кратный шагу от эпохи
        qint64 t = (m_minMs / step + 1) * step;
        for (; t <= m_maxMs; t += step) {
            const int x = xOfMs(t);
            p.setPen(QColor(160, 167, 176));
            p.drawLine(x, track.bottom() + 1, x, track.bottom() + 4);
            p.setPen(QColor(90, 98, 108));
            const QString label = formatTick(t, step);
            const int labelW = p.fontMetrics().horizontalAdvance(label);
            QRect labelRect(x - labelW / 2, track.bottom() + 5, labelW, kAxisHeight - 5);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, label);
        }

        // Диапазон под осью
        p.setPen(QColor(33, 150, 243));
        p.drawLine(xs, track.bottom() + 1, xe, track.bottom() + 1);
    } else {
        p.setPen(QColor(140, 146, 155));
        p.drawText(track, Qt::AlignCenter, tr("Нет событий для отображения"));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (!rangeAvailable() || event->button() != Qt::LeftButton)
        return;
    const int x = event->pos().x();
    const int xs = xOfMs(m_selStartMs);
    const int xe = xOfMs(m_selEndMs);

    const int dStart = qAbs(x - xs);
    const int dEnd = qAbs(x - xe);
    if (qMin(dStart, dEnd) <= kHandleHit) {
        m_drag = (dStart <= dEnd) ? Drag::Start : Drag::End;
        updateSelectionFromMouse(x);
    } else if (x > xs && x < xe) {
        // Клик внутри диапазона — двигаем ближайшую ручку
        m_drag = ((x - xs) <= (xe - x)) ? Drag::Start : Drag::End;
        updateSelectionFromMouse(x);
    } else {
        // Клик вне диапазона — приближаем ближайшую ручку
        m_drag = (dStart <= dEnd) ? Drag::Start : Drag::End;
        updateSelectionFromMouse(x);
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag == Drag::None)
        return;
    updateSelectionFromMouse(event->pos().x());
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    if (m_drag == Drag::None)
        return;
    m_drag = Drag::None;
    emit selectionChanged(m_selStartMs, m_selEndMs);
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    if (!rangeAvailable())
        return;
    // Двойной клик — выделить весь период
    setSelection(m_minMs, m_maxMs);
    m_drag = Drag::None;
    emit selectionChanged(m_selStartMs, m_selEndMs);
}

void TimelineWidget::updateSelectionFromMouse(int x)
{
    const qint64 ms = msOfX(x);
    if (m_drag == Drag::Start)
        setSelection(ms, m_selEndMs);
    else if (m_drag == Drag::End)
        setSelection(m_selStartMs, ms);
    update();
}
