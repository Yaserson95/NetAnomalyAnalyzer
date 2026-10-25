#include "FilterDialog.h"
#include "LogViewerWidget.h"
#include "TableSorter.h"
#include "TimelineWidget.h"
#include "../settings/Settings.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

#include <utility>

namespace {
/** Максимум считываемых записей из одного файла (защита от гигантских логов). */
constexpr int kMaxRecords = 200000;
/** Если файл больше — читаем только хвост (байт). */
constexpr qint64 kMaxReadBytes = 64LL * 1024 * 1024;
/** Максимум строк в таблице за выбранный период. */
constexpr int kMaxTableRows = 5000;

QString formatTime(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms)
        .toString(QStringLiteral("HH:mm:ss.zzz"));
}
} // namespace

LogViewerWidget::LogViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    // --- Строка выбора файла ---
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(new QLabel(tr("Файл журнала:"), this));
    m_fileCombo = new QComboBox(this);
    m_fileCombo->setMinimumWidth(280);
    fileRow->addWidget(m_fileCombo, 1);
    m_reloadBtn = new QPushButton(tr("Обновить"), this);
    fileRow->addWidget(m_reloadBtn);
    m_filterBtn = new QPushButton(tr("Фильтр…"), this);
    m_filterBtn->setToolTip(
        tr("Открыть диалог фильтрации записей: параметр — действие — значение\n"
           "(например, «MAC отправителя | начинается на | bc:24»)."));
    fileRow->addWidget(m_filterBtn);
    m_rangeLabel = new QLabel(this);
    m_rangeLabel->setStyleSheet(QStringLiteral("color:#666"));
    fileRow->addWidget(m_rangeLabel, 1);
    fileRow->addStretch(0);
    layout->addLayout(fileRow);

    // --- Временная шкала ---
    auto *hint = new QLabel(
        tr("Гистограмма событий во времени. Перетащите ручки, чтобы выбрать период "
           "(двойной клик — весь период). Таблица показывает записи за выбранный период."), this);
    hint->setStyleSheet(QStringLiteral("color:#666"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_timeline = new TimelineWidget(this);
    layout->addWidget(m_timeline);

    // --- Таблица записей + детали ---
    auto *splitter = new QSplitter(Qt::Vertical, this);
    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("Время"), tr("Событие"), tr("Содержание") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    {   // Восстановить сохранённый порядок сортировки
        QSettings s = Settings::openSettings();
        m_sortColumn = s.value(QStringLiteral("logSort/column"), 0).toInt();
        const int order = s.value(QStringLiteral("logSort/order"),
                                  int(Qt::AscendingOrder)).toInt();
        m_sortOrder = order == int(Qt::DescendingOrder)
            ? Qt::DescendingOrder : Qt::AscendingOrder;
        if (m_sortColumn < 0 || m_sortColumn >= m_table->columnCount()) {
            m_sortColumn = 0;
            m_sortOrder = Qt::AscendingOrder;
        }
        m_table->horizontalHeader()->setSortIndicator(m_sortColumn, m_sortOrder);
    }

    m_detail = new QPlainTextEdit(splitter);
    m_detail->setReadOnly(true);
    m_detail->setPlaceholderText(tr("Выберите запись, чтобы увидеть полное содержимое (JSON)…"));
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(9);
    m_detail->setFont(mono);

    splitter->addWidget(m_table);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    connect(m_reloadBtn, &QPushButton::clicked, this, &LogViewerWidget::refreshFiles);
    connect(m_filterBtn, &QPushButton::clicked, this,
            &LogViewerWidget::openFilterDialog);
    connect(m_fileCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { reload(); });
    connect(m_timeline, &TimelineWidget::selectionChanged, this,
            &LogViewerWidget::applyRange);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &LogViewerWidget::onRowSelected);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this,
            [this](int column) {
                if (column == m_sortColumn)
                    m_sortOrder = m_sortOrder == Qt::AscendingOrder
                        ? Qt::DescendingOrder : Qt::AscendingOrder;
                else {
                    m_sortColumn = column;
                    // Новая колонка: числа (например, время) — по убыванию,
                    // текст — по возрастанию
                    m_sortOrder = TableSorter::columnIsNumeric(m_table, column)
                        ? Qt::DescendingOrder : Qt::AscendingOrder;
                }
                // Запомнить выбранную запись (индекс в m_filteredIndices)
                const int row = m_table->currentRow();
                const QTableWidgetItem *item = row >= 0 ? m_table->item(row, 0) : nullptr;
                const QVariant selected = item ? item->data(Qt::UserRole) : QVariant();
                applySort();
                {   // Сохранить порядок сортировки в настройках
                    QSettings s = Settings::openSettings();
                    s.setValue(QStringLiteral("logSort/column"), m_sortColumn);
                    s.setValue(QStringLiteral("logSort/order"), int(m_sortOrder));
                }
                // Восстановить выделение: индекс записи переезжает со строкой
                if (selected.isValid()) {
                    for (int r = 0; r < m_table->rowCount(); ++r) {
                        const QTableWidgetItem *it = m_table->item(r, 0);
                        if (it && it->data(Qt::UserRole) == selected) {
                            m_table->setCurrentCell(r, 0);
                            m_table->selectRow(r);
                            break;
                        }
                    }
                }
            });

    loadFilterSettings();
    refreshFiles();
}

void LogViewerWidget::refreshFiles()
{
    const QString dir = Settings::instance().logging().logDir;
    const QString previous = m_fileCombo->currentData().toString();

    m_fileCombo->blockSignals(true);
    m_fileCombo->clear();

    QDir d(dir);
    // Свежие файлы по дате изменения, но сгруппируем по «семействам»:
    QStringList names = d.entryList({ QStringLiteral("*.jsonl"), QStringLiteral("*.jsonl.1") },
                                    QDir::Files, QDir::Name);
    names.sort(Qt::CaseInsensitive);
    for (const QString &name : std::as_const(names))
        m_fileCombo->addItem(name, d.absoluteFilePath(name));
    m_fileCombo->blockSignals(false);

    int idx = previous.isEmpty() ? -1 : m_fileCombo->findData(previous);
    if (idx < 0 && m_fileCombo->count() > 0) {
        // По умолчанию открываем журнал пакетов текущего дня, иначе аномалии
        const QString today = QStringLiteral("packets-%1.jsonl")
                                  .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
        idx = m_fileCombo->findText(today);
        if (idx < 0)
            idx = m_fileCombo->findText(QStringLiteral("anomalies.jsonl"));
        if (idx < 0)
            idx = 0;
    }
    if (idx >= 0) {
        m_fileCombo->setCurrentIndex(idx);
        reload();
    } else {
        m_records.clear();
        m_timeline->setEvents({});
        m_table->setRowCount(0);
        m_rangeLabel->setText(tr("Нет файлов журналов в «%1»").arg(dir));
    }
}

void LogViewerWidget::reload()
{
    m_currentPath = m_fileCombo->currentData().toString();
    if (m_currentPath.isEmpty())
        return;
    loadFile(m_currentPath);
}

void LogViewerWidget::loadFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_records.clear();
        m_timeline->setEvents({});
        m_table->setRowCount(0);
        m_rangeLabel->setText(tr("Не удалось открыть %1").arg(QFileInfo(path).fileName()));
        return;
    }

    QByteArray content = file.readAll();
    file.close();
    bool truncated = false;
    if (content.size() > kMaxReadBytes) {
        // Хвост файла: пропускаем возможный неполный хвост первой строки
        content = content.right(kMaxReadBytes);
        const int nl = content.indexOf('\n');
        if (nl >= 0)
            content = content.mid(nl + 1);
        truncated = true;
    }

    m_records.clear();
    const QList<QByteArray> lines = content.split('\n');
    m_records.reserve(qMin(lines.size(), kMaxRecords));
    for (const QByteArray &raw : std::as_const(lines)) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (m_records.size() >= kMaxRecords) {
            truncated = true;
            break;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        LogRecord rec;
        rec.json = doc.object();
        rec.timeMs = extractTimeMs(rec.json);
        m_records.append(rec);
    }

    // Сортировка по времени; записи без времени — в исходном порядке в начале
    std::stable_sort(m_records.begin(), m_records.end(),
                     [](const LogRecord &a, const LogRecord &b) {
                         if (a.timeMs == 0 || b.timeMs == 0)
                             return a.timeMs != 0 ? b.timeMs == 0 : false;
                         return a.timeMs < b.timeMs;
                     });

    // Данные для шкалы
    QVector<TimelineWidget::Event> events;
    events.reserve(m_records.size());
    const QString fileName = QFileInfo(path).fileName();
    for (const LogRecord &rec : std::as_const(m_records)) {
        if (rec.timeMs > 0)
            events.append({ rec.timeMs, eventColor(fileName, rec.json) });
    }
    m_timeline->setEvents(events);

    m_baseLabel =
        truncated ? tr("%1: >%2 записей (показан хвост файла)")
                        .arg(fileName)
                        .arg(m_records.size())
                  : tr("%1: %2 записей").arg(fileName).arg(m_records.size());
    m_rangeLabel->setText(m_baseLabel);

    // Таблица за весь период
    applyRange(m_timeline->selectionStart(), m_timeline->selectionEnd());
}

void LogViewerWidget::applyRange(qint64 startMs, qint64 endMs)
{
    const QString fileName = QFileInfo(m_currentPath).fileName();
    m_table->setRowCount(0);
    m_filteredIndices.clear();

    int shown = 0;
    int total = 0;
    for (int i = 0; i < m_records.size(); ++i) {
        const LogRecord &rec = m_records.at(i);
        // Записи без времени показываем всегда (они не участвуют в шкале)
        const bool inRange = rec.timeMs == 0
            || (rec.timeMs >= startMs && rec.timeMs <= endMs);
        if (!inRange)
            continue;
        // Условия фильтра (все должны выполняться — «И»)
        bool matches = true;
        for (const FilterCondition &condition : std::as_const(m_filterConditions)) {
            if (!condition.matchesJson(rec.json)) {
                matches = false;
                break;
            }
        }
        if (!matches)
            continue;
        ++total;
        m_filteredIndices.append(i);
        if (shown >= kMaxTableRows)
            continue;

        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *timeItem = new QTableWidgetItem(
            rec.timeMs > 0 ? formatTime(rec.timeMs) : QStringLiteral("—"));
        timeItem->setData(Qt::UserRole, m_filteredIndices.size() - 1);   // индекс в отфильтрованных
        timeItem->setData(Qt::UserRole + 1, double(rec.timeMs));         // сырое время для сортировки
        m_table->setItem(row, 0, timeItem);
        m_table->setItem(row, 1, new QTableWidgetItem(summarize(fileName, rec.json)));
        m_table->setItem(row, 2,
                         new QTableWidgetItem(QString::fromUtf8(
                             QJsonDocument(rec.json).toJson(QJsonDocument::Compact))));
        ++shown;
    }

    m_limitRow = (total > shown) ? 1 : 0;
    if (total > shown) {
        m_table->insertRow(m_table->rowCount());
        m_table->setItem(m_table->rowCount() - 1, 1,
                         new QTableWidgetItem(tr("… и ещё %1 записей (ограничение отображения)")
                                                  .arg(total - shown)));
    }

    QString filterText;
    if (!m_filterConditions.isEmpty()) {
        QStringList parts;
        for (const FilterCondition &condition : std::as_const(m_filterConditions))
            parts << condition.describe();
        filterText = tr("  |  Фильтр: %1").arg(parts.join(QStringLiteral(" И ")));
    }

    if (m_timeline->rangeAvailable()) {
        const QString from = QDateTime::fromMSecsSinceEpoch(startMs)
                                 .toString(QStringLiteral("dd.MM HH:mm:ss"));
        const QString to = QDateTime::fromMSecsSinceEpoch(endMs)
                               .toString(QStringLiteral("dd.MM HH:mm:ss"));
        m_rangeLabel->setText(m_baseLabel +
                              tr("  |  Период: %1 — %2, записей: %3").arg(from, to)
                                  .arg(total) +
                              filterText);
    } else {
        m_rangeLabel->setText(m_baseLabel +
                              (m_filterConditions.isEmpty()
                                   ? QString()
                                   : tr("  |  записей с учётом фильтра: %1").arg(total) +
                                         filterText));
    }

    // Порядок, выбранный кликом по заголовку, сохраняется при перестроении
    applySort();
}

void LogViewerWidget::applySort()
{
    TableSorter::sortRows(m_table, m_sortColumn, m_sortOrder, m_limitRow);
    m_table->horizontalHeader()->setSortIndicator(m_sortColumn, m_sortOrder);
}

void LogViewerWidget::openFilterDialog()
{
    FilterDialog dialog(m_filterConditions, FilterCondition::logFields(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_filterConditions = dialog.conditions();
    saveFilterSettings();
    applyRange(m_timeline->selectionStart(), m_timeline->selectionEnd());
}

void LogViewerWidget::loadFilterSettings()
{
    QSettings s = Settings::openSettings();
    const int count = s.beginReadArray(QStringLiteral("filters/logRecords"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        FilterCondition condition;
        condition.field = static_cast<FilterCondition::Field>(
            s.value(QStringLiteral("field"), 0).toInt());
        condition.op = static_cast<FilterCondition::Op>(
            s.value(QStringLiteral("op"), 0).toInt());
        condition.value = s.value(QStringLiteral("value")).toString();
        if (!condition.value.isEmpty())
            m_filterConditions.append(condition);
    }
    s.endArray();
}

void LogViewerWidget::saveFilterSettings()
{
    QSettings s = Settings::openSettings();
    s.remove(QStringLiteral("filters/logRecords"));
    if (m_filterConditions.isEmpty())
        return;
    s.beginWriteArray(QStringLiteral("filters/logRecords"), m_filterConditions.size());
    for (int i = 0; i < m_filterConditions.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("field"),
                   static_cast<int>(m_filterConditions.at(i).field));
        s.setValue(QStringLiteral("op"),
                   static_cast<int>(m_filterConditions.at(i).op));
        s.setValue(QStringLiteral("value"), m_filterConditions.at(i).value);
    }
    s.endArray();
}

void LogViewerWidget::onRowSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QTableWidgetItem *item = m_table->item(row, 0);
    const QVariant idxData = item ? item->data(Qt::UserRole) : QVariant();
    if (!idxData.isValid()) {
        // Строка-ограничитель
        return;
    }

    // Индекс указывает в список записей, прошедших фильтр периода и условий
    const int recordIndex = m_filteredIndices.value(idxData.toInt(), -1);
    if (recordIndex >= 0) {
        m_detail->setPlainText(QString::fromUtf8(
            QJsonDocument(m_records.at(recordIndex).json).toJson(QJsonDocument::Indented)));
    }
}

QString LogViewerWidget::summarize(const QString &fileName, const QJsonObject &o)
{
    if (fileName.startsWith(QLatin1String("packets-"))) {
        // info уже содержит полную строку вида «TCP a:p → b:q [PSH,ACK]»
        const QString info = o.value(QStringLiteral("info")).toString();
        if (!info.isEmpty())
            return info;
        return QStringLiteral("%1 %2:%3 → %4:%5")
            .arg(o.value(QStringLiteral("protocol")).toString(),
                 o.value(QStringLiteral("src")).toString(),
                 QString::number(o.value(QStringLiteral("srcPort")).toInt()),
                 o.value(QStringLiteral("dst")).toString(),
                 QString::number(o.value(QStringLiteral("dstPort")).toInt()));
    }
    if (fileName.startsWith(QLatin1String("anomalies")) ||
        fileName.startsWith(QLatin1String("notifications"))) {
        const QString status = o.value(QStringLiteral("status")).toString();
        return QStringLiteral("[%1] %2 — %3%4%5")
            .arg(o.value(QStringLiteral("severity")).toString(),
                 o.value(QStringLiteral("rule")).toString(),
                 o.value(QStringLiteral("title")).toString(),
                 fileName.startsWith(QLatin1String("notifications"))
                     ? QStringLiteral(" (%1, %2)")
                           .arg(o.value(QStringLiteral("channel")).toString(),
                                o.value(QStringLiteral("status")).toString())
                     : QString(),
                 // anomalies.jsonl + подмешанный tailAnomalies статус «решено»
                 (fileName.startsWith(QLatin1String("anomalies"))
                  && status == QLatin1String("resolved"))
                     ? QStringLiteral("  [решено]") : QString());
    }
    if (fileName.startsWith(QLatin1String("errors"))) {
        return QStringLiteral("%1: %2")
            .arg(o.value(QStringLiteral("subsystem")).toString(),
                 o.value(QStringLiteral("message")).toString());
    }
    if (fileName.startsWith(QLatin1String("llm-journal"))) {
        return QStringLiteral("%1, %2, %3 мс — %4")
            .arg(o.value(QStringLiteral("source")).toString(),
                 o.value(QStringLiteral("status")).toString(),
                 QString::number(o.value(QStringLiteral("elapsedMs")).toInt()),
                 o.value(QStringLiteral("model")).toString());
    }
    // Прочие файлы: первые строковые значения
    QStringList parts;
    for (auto it = o.constBegin(); it != o.constEnd() && parts.size() < 3; ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty())
            parts.append(QStringLiteral("%1: %2").arg(it.key(), it.value().toString()));
    }
    return parts.join(QStringLiteral("; "));
}

QColor LogViewerWidget::eventColor(const QString &fileName, const QJsonObject &o)
{
    if (fileName.startsWith(QLatin1String("errors")))
        return QColor(192, 57, 43);
    if (fileName.startsWith(QLatin1String("llm-journal")))
        return QColor(142, 68, 173);
    if (fileName.startsWith(QLatin1String("notifications")))
        return QColor(211, 84, 0);
    if (fileName.startsWith(QLatin1String("anomalies"))) {
        const QString sev = o.value(QStringLiteral("severity")).toString();
        if (sev.contains(QStringLiteral("Крит"), Qt::CaseInsensitive) ||
            sev.contains(QStringLiteral("Critical"), Qt::CaseInsensitive))
            return QColor(192, 57, 43);
        if (sev.contains(QStringLiteral("Предупреж"), Qt::CaseInsensitive) ||
            sev.contains(QStringLiteral("Warning"), Qt::CaseInsensitive))
            return QColor(214, 137, 16);
        return QColor(127, 140, 141);
    }
    return QColor(52, 120, 198);   // пакеты и всё остальное
}

qint64 LogViewerWidget::extractTimeMs(const QJsonObject &o)
{
    static const char *keys[] = { "timestamp", "sentAt", "startTime", "finishTime" };
    for (const char *key : keys) {
        const QString value = o.value(QLatin1String(key)).toString();
        if (value.isEmpty())
            continue;
        const QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
        if (dt.isValid())
            return dt.toMSecsSinceEpoch();
    }
    return 0;
}
