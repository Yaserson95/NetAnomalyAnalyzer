#pragma once

#include "Notifier.h"

#include <QObject>
#include <QVector>

class LogWriter;

/**
 * @brief Менеджер оповещений: рассылает аномалию по всем подключённым
 * каналам (журнал, Telegram, вебхук, электронная почта) с учётом
 * настроек и фильтра по статусам: сообщения с
 * невыбранным уровнем не отправляются (кроме журнала notifications.jsonl).
 *
 * GUI-оповещения (всплывающие окна/трей) обрабатываются отдельно
 * через сигнал guiNotification() — он тоже учитывает фильтр статусов.
 */
class NotificationManager : public QObject {
    Q_OBJECT
public:
    explicit NotificationManager(LogWriter *logWriter, QObject *parent = nullptr);

    QVector<Notifier *> notifiers() const { return m_notifiers; }

    /** Отправить тестовое сообщение во все включённые каналы. */
    void sendTestMessage();

public slots:
    void onAnomaly(const Anomaly &anomaly);

signals:
    /** Для всплывающих окон и трей-уведомлений в главном окне. */
    void guiNotification(const Anomaly &anomaly);
    void notificationStatus(const QString &channel, const QString &status);
    /**
     * Запрос звукового сигнала: ядро не зависит от GUI, поэтому звук
     * воспроизводит оболочка (MainWindow → QApplication::beep); в
     * консольном/служебном режиме сигнал ни к чему не подключён.
     */
    void alertSound();

private:
    QVector<Notifier *> m_notifiers;
    LogWriter          *m_logWriter = nullptr;
};
