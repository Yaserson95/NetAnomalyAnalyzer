#pragma once

#include "../core/FilterCondition.h"
#include "../core/PacketGrouper.h"

#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QWidget>
#include <QVector>

#include <thread>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QRadioButton;
class QTableWidget;
class PacketTableModel;

/**
 * @brief Вкладка «Группировка»: группировка потока пакетов (или записей
 * журнала packets-*.jsonl) по одному-трём параметрам со сводной
 * информацией по каждой группе (пакеты, байты, доля, интервал времени).
 *
 * Источник данных: «текущий поток» — буфер таблицы «Пакеты» (живые
 * пакеты); «журнал пакетов» — выбранный JSONL-файл за день.
 *
 * Большой журнал читается в фоновом потоке: файл читается с хвоста
 * (не более 64 МБ за раз), а при росте журнала дозагружаются только
 * новые записи — интерфейс не блокируется даже на многогигабайтных
 * файлах.
 */
class GroupingWidget : public QWidget {
    Q_OBJECT
public:
    explicit GroupingWidget(PacketTableModel *packetModel, QWidget *parent = nullptr);
    ~GroupingWidget() override;

    /** Задача чтения и группировки журнала (фоновый поток). */
    struct JournalTask {
        quint64                         seq = 0;   ///< номер задачи (устаревание)
        QString                         path;      ///< файл журнала
        QVector<FilterCondition::Field> fields;    ///< поля группировки
    };

    /** Результат задачи журнала (применяется в GUI-потоке). */
    struct JournalOutcome {
        quint64               seq = 0;
        bool                  ok = false;      ///< журнал прочитан
        QString               absolutePath;    ///< путь прочитанного файла
        QString               fileName;
        qint64                size = -1;       ///< размер файла при чтении
        QDateTime             mtime;           ///< время изменения при чтении
        int                   records = 0;     ///< разобрано записей
        PacketGrouper::Result result;          ///< готовая группировка
    };

public slots:
    /** Обновить таблицу при необходимости (таймер/активация вкладки):
     *  живой режим — не чаще раза в 800 мс; журнал — только если файл
     *  изменился (и не чаще раза в 2 с), чтобы не сбрасывать выделение. */
    void refresh();

private:
    /** Форсированный пересчёт групп (при смене источника/параметров). */
    void recompute();
    /** Запустить задачу журнала в фоне (интерфейс не блокируется). */
    void startJournalJob(const QString &path,
                         const QVector<FilterCondition::Field> &fields);
    /** Чтение/дозагрузка записей + группировка (рабочий поток). */
    static JournalOutcome executeJournalTask(const JournalTask &task,
                                             QVector<QJsonObject> &records,
                                             QString &recPath, qint64 &recEnd,
                                             qint64 &recSize, QDateTime &recMtime);
    /** Применить результат задачи журнала (GUI-поток). */
    void applyJournalOutcome(const JournalOutcome &outcome);
    /** Заполнить комбобоксы параметров группировки. */
    void buildFieldCombos();
    /** Заполнить список журналов пакетов (packets-*.jsonl). */
    void refreshLogFiles();
    /** Собрать выбранные поля (с удалением дублей и пустых). */
    QVector<FilterCondition::Field> selectedFields() const;
    /** Построить таблицу по результатам группировки. */
    void fillTable(const PacketGrouper::Result &result);
    /** Сохранить/восстановить выбор источника и полей в настройках. */
    void saveState();
    void restoreState();
    /** Ключи выбранной группы / восстановление выделения по ключам. */
    QStringList selectedGroupKeys() const;
    void restoreSelection(const QStringList &keys);
    /** Применить текущую сортировку (m_sortColumn/m_sortOrder) к таблице. */
    void applySort();

    PacketTableModel *m_packetModel = nullptr;

    QRadioButton *m_sourceStream = nullptr;   ///< источник: текущий поток
    QRadioButton *m_sourceLog    = nullptr;   ///< источник: журнал пакетов
    QComboBox    *m_logCombo     = nullptr;   ///< выбор файла журнала
    QComboBox    *m_field1 = nullptr;
    QComboBox    *m_field2 = nullptr;
    QComboBox    *m_field3 = nullptr;
    QLabel       *m_summaryLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_detail = nullptr;

    // --- Кэш записей журнала: принадлежит рабочему потоку задач ---
    // Читается и пишется только во время выполнения задачи (m_jobRunning);
    // GUI-поток эти поля не трогает, результат приходит через JournalOutcome.
    QVector<QJsonObject> m_records;           ///< разобранные записи журнала
    QString     m_recPath;                    ///< файл кэша записей
    qint64      m_recEnd = 0;                 ///< байт прочитано до последней '\n'
    qint64      m_recSize = -1;               ///< размер файла при загрузке
    QDateTime   m_recMtime;                   ///< время файла при загрузке

    // --- Фоновая задача журнала (только GUI-поток) ---
    std::thread m_worker;                     ///< поток задач журнала
    bool        m_jobRunning = false;         ///< задача сейчас выполняется
    bool        m_jobAgain = false;           ///< перезапустить после текущей
    quint64     m_jobSeq = 0;                 ///< счётчик задач (устаревание)
    QString     m_jobPath;                    ///< путь последней задачи
    QVector<FilterCondition::Field> m_jobFields;   ///< её поля группировки

    QString     m_loadedFile;                 ///< применённый файл (для refresh)
    qint64      m_loadedFileSize = -1;        ///< его размер при применении
    QDateTime   m_loadedFileMtime;            ///< его время изменения
    qint64      m_lastStreamMs = 0;           ///< последний пересчёт «потока»
    qint64      m_lastJournalMs = 0;          ///< последний пересчёт журнала
    int         m_sortColumn = -1;            ///< колонка сортировки (-1 — задана ядром)
    Qt::SortOrder m_sortOrder = Qt::DescendingOrder;
    bool        m_sortInitialized = false;    ///< сортировка по умолчанию выставлена
    int         m_limitRows = 0;              ///< строк-ограничителей «… и ещё»
};
