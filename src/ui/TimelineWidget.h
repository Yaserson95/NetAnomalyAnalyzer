#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

/**
 * @brief Временная шкала событий для просмотра логов.
 *
 * Рисует гистограмму событий по времени (столбики высотой по плотности,
 * цвет — по типу события), шкалу времени с подписями и перетаскиваемый
 * диапазон выборки с двумя ручками. Изменение диапазона сообщаетается
 * сигналом selectionChanged(startMs, endMs).
 */
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    struct Event {
        qint64  timeMs = 0;
        QColor  color  = QColor(52, 120, 198);
    };

    explicit TimelineWidget(QWidget *parent = nullptr);

    /** Задать события: шкала и выборка сбрасываются на весь период. */
    void setEvents(QVector<Event> events);

    /** Текущий выделенный диапазон (мс от эпохи Unix). */
    qint64 selectionStart() const { return m_selStartMs; }
    qint64 selectionEnd() const { return m_selEndMs; }

    /** События попадают в диапазон по всему интервалу включительно. */
    bool rangeAvailable() const { return m_maxMs > m_minMs; }

    QSize minimumSizeHint() const override { return QSize(240, 76); }
    QSize sizeHint() const override { return QSize(600, 76); }

public slots:
    /** Выделить весь доступный период (если шкала пуста — без действия). */
    void selectFullRange();

signals:
    void selectionChanged(qint64 startMs, qint64 endMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    static constexpr int kMargin = 10;
    static constexpr int kAxisHeight = 16;   ///< полоса подписей времени
    static constexpr int kHandleHit = 10;    ///< зона захвата ручки, px
    static constexpr qint64 kMinSelectionMs = 500;

    int xOfMs(qint64 ms) const;
    qint64 msOfX(int x) const;
    int trackTop() const;
    int trackBottom() const;
    void setSelection(qint64 startMs, qint64 endMs);
    void updateSelectionFromMouse(int x);
    void ensureVisibleRange(qint64 &ms) const;

    QVector<Event> m_events;
    qint64 m_minMs = 0;
    qint64 m_maxMs = 0;
    qint64 m_selStartMs = 0;
    qint64 m_selEndMs = 0;

    enum class Drag { None, Start, End };
    Drag m_drag = Drag::None;
};
