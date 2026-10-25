#pragma once

#include <QMap>
#include <QString>
#include <QVector>
#include <QWidget>

/**
 * @brief Линейный график загруженности: количество пакетов во времени
 * для каждого прослушиваемого интерфейса (собственная отрисовка).
 */
class TimeSeriesWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimeSeriesWidget(QWidget *parent = nullptr);

    /** Данные: имя интерфейса -> ряд значений (пакетов/сек). */
    void setSeries(const QMap<QString, QVector<double>> &series);
    void setTitle(const QString &title) { m_title = title; update(); }
    void setYLabel(const QString &label) { m_yLabel = label; update(); }

    QSize sizeHint() const override { return { 640, 240 }; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QMap<QString, QVector<double>> m_series;
    QString m_title;
    QString m_yLabel;
};
