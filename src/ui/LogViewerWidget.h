#pragma once

#include "../core/FilterCondition.h"

#include <QJsonObject>
#include <QVector>
#include <QWidget>

class TimelineWidget;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

/**
 * @brief Вкладка «Просмотр логов»: чтение журнальных JSONL-файлов с
 * временной шкалой (гистограмма событий + перетаскиваемый диапазон),
 * таблицей записей за выбранный период и панелью полного содержимого.
 */
class LogViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogViewerWidget(QWidget *parent = nullptr);

public slots:
    /** Перечислить файлы журналов и перезагрузить текущий. */
    void refreshFiles();
    /** Перечитать выбранный файл (например, при активации вкладки). */
    void reload();

private slots:
    void openFilterDialog();

private:
    /** Одна запись журнала: момент времени + сырой JSON. */
    struct LogRecord {
        qint64      timeMs = 0;   ///< 0 — метка времени отсутствует/не распознана
        QJsonObject json;
    };

    void loadFile(const QString &path);
    void applyRange(qint64 startMs, qint64 endMs);
    /** Применить текущую сортировку (m_sortColumn/m_sortOrder) к таблице. */
    void applySort();
    void onRowSelected();
    /** Фильтр записей: восстановить из настроек / сохранить в них. */
    void loadFilterSettings();
    void saveFilterSettings();
    /** Краткое текстовое описание записи в зависимости от журнала. */
    static QString summarize(const QString &fileName, const QJsonObject &o);
    /** Цвет события для временной шкалы. */
    static QColor eventColor(const QString &fileName, const QJsonObject &o);
    /** Момент времени записи: timestamp / sentAt / startTime. */
    static qint64 extractTimeMs(const QJsonObject &o);

    QVector<LogRecord> m_records;      ///< все записи файла (отсортированы по времени)
    QVector<int>       m_filteredIndices;   ///< индексы записей, прошедших фильтр
    QString             m_currentPath;
    QString             m_baseLabel;   ///< текст о файле (без части про период)

    QComboBox      *m_fileCombo = nullptr;
    QPushButton    *m_reloadBtn = nullptr;
    QPushButton    *m_filterBtn = nullptr;
    TimelineWidget *m_timeline = nullptr;
    QLabel         *m_rangeLabel = nullptr;
    QTableWidget   *m_table = nullptr;
    QPlainTextEdit *m_detail = nullptr;

    QVector<FilterCondition> m_filterConditions;   ///< условия фильтра записей
    int         m_sortColumn = 0;                 ///< колонка сортировки (по умолчанию «Время»)
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    int         m_limitRow = 0;                   ///< строка-ограничитель «… и ещё»
};
