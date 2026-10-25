#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

/**
 * @brief Горизонтальная диаграмма распределения пакетов по протоколам.
 */
class ProtocolBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ProtocolBarWidget(QWidget *parent = nullptr);

    void setCounts(const QHash<QString, qint64> &counts);
    void setTitle(const QString &title) { m_title = title; update(); }
    QSize sizeHint() const override { return { 320, 240 }; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QHash<QString, qint64> m_counts;
    QString m_title;
};
