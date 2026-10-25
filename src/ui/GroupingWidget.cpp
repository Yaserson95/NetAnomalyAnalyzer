#include "GroupingWidget.h"

#include "../core/Packet.h"
#include "../settings/Settings.h"
#include "PacketTableModel.h"
#include "TableSorter.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstring>
#include <utility>

namespace {
/** Максимум считываемых записей журнала. */
constexpr int kMaxRecords = 200000;
/** Если файл больше — читаем только хвост (байт). */
constexpr qint64 kMaxReadBytes = 64LL * 1024 * 1024;
/** Показываем не более стольких групп (остальные — «… и ещё»). */
constexpr int kMaxGroupsShown = 5000;
/** Маркер пустого поля в комбобоксе. */
constexpr int kNoField = -1;

QString formatTimeMs(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 ГБ").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (bytes >= 1024LL * 1024)
        return QStringLiteral("%1 МБ").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 КБ").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 Б").arg(bytes);
}

/**
 * Разобрать буфер журнала (JSON Lines) и дозаполнить records.
 *
 * Разбираются только завершённые строки — заканчивающиеся на '\n'
 * (неполная последняя строка ждёт дописывания в файл); пустые строки
 * и не-JSON отбрасываются без аллокаций. Возвращает число байт буфера,
 * перекрытых разобранными строками (смещение для продолжения чтения
 * приращением). Останавливается по лимиту kMaxRecords.
 */
qint64 parseJournalLines(const char *data, qsizetype size,
                         QVector<QJsonObject> &records)
{
    qint64 consumed = 0;
    qsizetype start = 0;
    while (records.size() < kMaxRecords) {
        const void *nl = std::memchr(data + start, '\n', size - start);
        if (!nl)
            break;   // остаток — неполная строка
        const qsizetype end = static_cast<const char *>(nl) - data;
        const char *begin = data + start;
        while (begin < data + end
               && (*begin == ' ' || *begin == '\t' || *begin == '\r'))
            ++begin;
        if (begin < data + end && *begin == '{') {
            const QJsonDocument doc =
                QJsonDocument::fromJson(QByteArray(begin, end - (begin - data)));
            if (doc.isObject())
                records.append(doc.object());
        }
        start = end + 1;
        consumed = start;
    }
    return consumed;
}
} // namespace

GroupingWidget::GroupingWidget(PacketTableModel *packetModel, QWidget *parent)
    : QWidget(parent)
    , m_packetModel(packetModel)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    // --- Источник данных ---
    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new QLabel(tr("Источник:"), this));
    m_sourceStream = new QRadioButton(tr("Текущий поток (буфер таблицы «Пакеты»)"), this);
    m_sourceLog = new QRadioButton(tr("Журнал пакетов:"), this);
    m_sourceStream->setChecked(true);
    sourceRow->addWidget(m_sourceStream);
    sourceRow->addWidget(m_sourceLog);
    m_logCombo = new QComboBox(this);
    m_logCombo->setMinimumWidth(260);
    m_logCombo->setEnabled(false);
    sourceRow->addWidget(m_logCombo, 1);
    layout->addLayout(sourceRow);

    connect(m_sourceStream, &QRadioButton::toggled, this, [this](bool on) {
        m_logCombo->setEnabled(!on);
        if (on) {
            saveState();
            recompute();
        }
    });
    connect(m_sourceLog, &QRadioButton::toggled, this, [this](bool on) {
        if (on) {
            saveState();
            recompute();
        }
    });
    connect(m_logCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_sourceLog->isChecked()) {
            m_loadedFile.clear();
            saveState();
            recompute();
        }
    });

    // --- Параметры группировки ---
    auto *fieldRow = new QHBoxLayout;
    fieldRow->addWidget(new QLabel(tr("Группировать по:"), this));
    auto makeCombo = [this, fieldRow](QComboBox **combo) {
        *combo = new QComboBox(this);
        fieldRow->addWidget(*combo, 1);
        connect(*combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    // Смена параметров меняет состав колонок — сортировка к ним
                    // больше не привязана, возвращаем порядок по умолчанию
                    m_sortColumn = -1;
                    m_sortInitialized = false;
                    saveState();
                    recompute();
                });
    };
    makeCombo(&m_field1);
    makeCombo(&m_field2);
    makeCombo(&m_field3);
    buildFieldCombos();
    layout->addLayout(fieldRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setStyleSheet(QStringLiteral("color:#666"));
    layout->addWidget(m_summaryLabel);

    // --- Таблица групп + детали ---
    auto *splitter = new QSplitter(Qt::Vertical, this);
    m_table = new QTableWidget(splitter);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this,
            [this](int column) {
                if (column == m_sortColumn && m_sortInitialized) {
                    m_sortOrder = m_sortOrder == Qt::AscendingOrder
                        ? Qt::DescendingOrder : Qt::AscendingOrder;
                } else {
                    m_sortColumn = column;
                    // Новая колонка: числа — по убыванию, текст — по возрастанию
                    m_sortOrder = TableSorter::columnIsNumeric(m_table, column)
                        ? Qt::DescendingOrder : Qt::AscendingOrder;
                    m_sortInitialized = true;
                }
                const QStringList keys = selectedGroupKeys();
                applySort();
                restoreSelection(keys);
                saveState();   // порядок сортировки — в настройках
            });
    m_detail = new QPlainTextEdit(splitter);
    m_detail->setReadOnly(true);
    m_detail->setPlaceholderText(
        tr("Выберите группу, чтобы увидеть подробную сводку…"));
    splitter->addWidget(m_table);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    auto updateDetail = [this] {
        const int row = m_table->currentRow();
        const QTableWidgetItem *item = row >= 0 ? m_table->item(row, 0) : nullptr;
        if (!item)
            return;
        m_detail->setPlainText(item->data(Qt::UserRole).toString());
    };
    connect(m_table, &QTableWidget::itemSelectionChanged, this, updateDetail);
    connect(m_table, &QTableWidget::currentCellChanged, this, updateDetail);

    restoreState();
    refreshLogFiles();
    recompute();
}

void GroupingWidget::buildFieldCombos()
{
    const QVector<FilterCondition::Field> fields = PacketGrouper::groupableFields();
    for (QComboBox *combo : { m_field1, m_field2, m_field3 }) {
        combo->blockSignals(true);
        combo->clear();
        combo->addItem(tr("(нет)"), kNoField);
        for (const FilterCondition::Field field : fields)
            combo->addItem(FilterCondition::fieldName(field), static_cast<int>(field));
        // Значения по умолчанию: IP отправителя / IP назначения / нет
        // (без сигналов — таблицы ещё нет в конструкторе)
        if (combo == m_field1)
            combo->setCurrentIndex(combo->findData(
                static_cast<int>(FilterCondition::Field::SrcIp)));
        else if (combo == m_field2)
            combo->setCurrentIndex(combo->findData(
                static_cast<int>(FilterCondition::Field::DstIp)));
        else
            combo->setCurrentIndex(0);
        combo->blockSignals(false);
    }
}

void GroupingWidget::refreshLogFiles()
{
    const QString previous = m_logCombo->currentData().toString();
    m_logCombo->blockSignals(true);
    m_logCombo->clear();

    const QString dir = Settings::instance().logging().logDir;
    QDir d(dir);
    const QStringList names = d.entryList({ QStringLiteral("packets-*.jsonl"),
                                            QStringLiteral("packets-*.jsonl.1") },
                                          QDir::Files, QDir::Name);
    for (const QString &name : names)
        m_logCombo->addItem(name, d.absoluteFilePath(name));
    m_logCombo->blockSignals(false);

    int idx = previous.isEmpty() ? -1 : m_logCombo->findData(previous);
    if (idx < 0 && m_logCombo->count() > 0) {
        const QString today = QStringLiteral("packets-%1.jsonl")
                                  .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
        idx = m_logCombo->findText(today);
        if (idx < 0)
            idx = m_logCombo->count() - 1;   // самый свежий файл
    }
    if (idx >= 0) {
        m_logCombo->blockSignals(true);
        m_logCombo->setCurrentIndex(idx);
        m_logCombo->blockSignals(false);
    }
}

QVector<FilterCondition::Field> GroupingWidget::selectedFields() const
{
    QVector<FilterCondition::Field> fields;
    for (const QComboBox *combo : { m_field1, m_field2, m_field3 }) {
        const int id = combo->currentData().toInt();
        if (id == kNoField)
            continue;
        const auto field = static_cast<FilterCondition::Field>(id);
        if (!fields.contains(field))
            fields.append(field);
    }
    return fields;
}

void GroupingWidget::refresh()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_sourceLog->isChecked()) {
        // Журнал: пересчитываем, только если файл изменился, и не чаще раза в 2 с
        const QFileInfo info(m_logCombo->currentData().toString());
        const bool changed = info.absoluteFilePath().isEmpty()
            || m_loadedFile != info.absoluteFilePath()
            || m_loadedFileSize != info.size()
            || m_loadedFileMtime != info.lastModified();
        if (changed && (m_table->rowCount() == 0 || now - m_lastJournalMs >= 2000))
            recompute();
    } else if (now - m_lastStreamMs >= 800) {
        // Живой режим: обновляем не чаще раза в 800 мс — таблица не мигает,
        // выделение группы не сбрасывается каждый тик батчинга
        recompute();
    }
}

void GroupingWidget::recompute()
{
    const QVector<FilterCondition::Field> fields = selectedFields();
    if (fields.isEmpty()) {
        ++m_jobSeq;                 // возможная задача журнала устаревает
        m_jobAgain = false;
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        m_summaryLabel->setText(tr("Выбери хотя бы один параметр группировки."));
        m_detail->clear();
        return;
    }

    if (m_sourceLog->isChecked()) {
        const QString path = m_logCombo->currentData().toString();
        if (path.isEmpty()) {
            ++m_jobSeq;
            m_jobAgain = false;
            m_table->setRowCount(0);
            m_table->setColumnCount(0);
            m_summaryLabel->setText(tr("Нет журналов пакетов (packets-*.jsonl). "
                                       "Включите запись пакетов в Настройки → Журналирование."));
            m_detail->clear();
            return;
        }
        // Задача с теми же параметрами уже выполняется — дождаемся её
        // результата: повторный запуск сделал бы его устаревшим и
        // таймер refresh() не дождался бы никогда применения
        if (m_jobRunning && path == m_jobPath && fields == m_jobFields)
            return;
        // Чтение журнала и группировка — в фоновом потоке
        startJournalJob(path, fields);
        return;
    }

    ++m_jobSeq;                     // задача журнала (если есть) устаревает
    m_jobAgain = false;
    QVector<Packet> packets;
    const int count = m_packetModel->rowCount();
    packets.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (const Packet *packet = m_packetModel->packetAt(i))
            packets.append(*packet);
    }
    const PacketGrouper::Result result = PacketGrouper::groupPackets(packets, fields);
    fillTable(result);
    m_summaryLabel->setText(
        tr("Групп: %1, пакетов: %2, байт: %3 — %4")
            .arg(result.groups.size())
            .arg(result.packets)
            .arg(formatBytes(result.bytes))
            .arg(tr("источник: текущий поток (%1 пакетов в буфере)").arg(count)));
    m_lastStreamMs = QDateTime::currentMSecsSinceEpoch();
}

void GroupingWidget::startJournalJob(const QString &path,
                                     const QVector<FilterCondition::Field> &fields)
{
    ++m_jobSeq;                     // предыдущая задача (если была) устаревает
    m_jobPath = path;               // запоминаем желаемые параметры
    m_jobFields = fields;
    if (m_jobRunning) {
        // Задача уже идёт: после её завершения запустим новую
        // с актуальными параметрами (см. обработку завершения ниже)
        m_jobAgain = true;
        return;
    }
    if (m_worker.joinable())
        m_worker.join();   // завершённый поток — присоединяем мгновенно
    m_jobRunning = true;
    m_summaryLabel->setText(
        tr("Чтение журнала %1…").arg(QFileInfo(path).fileName()));

    const quint64 seq = m_jobSeq;
    m_worker = std::thread([this, seq, path, fields] {
        const JournalOutcome outcome = executeJournalTask(
            { seq, path, fields }, m_records, m_recPath, m_recEnd,
            m_recSize, m_recMtime);
        // Готовый результат — в GUI-поток (ожидание не блокирует интерфейс)
        QMetaObject::invokeMethod(this, [this, outcome] {
            m_jobRunning = false;
            if (outcome.seq == m_jobSeq)
                applyJournalOutcome(outcome);
            if (m_jobAgain) {
                m_jobAgain = false;
                startJournalJob(m_logCombo->currentData().toString(),
                                selectedFields());
            }
        }, Qt::QueuedConnection);
    });
}

GroupingWidget::JournalOutcome GroupingWidget::executeJournalTask(
    const JournalTask &task, QVector<QJsonObject> &records,
    QString &recPath, qint64 &recEnd, qint64 &recSize, QDateTime &recMtime)
{
    JournalOutcome outcome;
    outcome.seq = task.seq;

    const QFileInfo info(task.path);
    outcome.absolutePath = info.absoluteFilePath();
    outcome.fileName = info.fileName();
    outcome.size = info.size();
    outcome.mtime = info.lastModified();

    // Кэш актуален (файл не менялся) — перечитывать не нужно,
    // достаточно перегруппировать записи (поля могли смениться)
    if (recPath == task.path && recSize == outcome.size
        && recMtime == outcome.mtime) {
        outcome.ok = true;
        outcome.records = records.size();
        outcome.result = PacketGrouper::groupRecords(records, task.fields);
        return outcome;
    }

    QFile file(task.path);
    if (!file.open(QIODevice::ReadOnly))
        return outcome;   // ok = false: файла нет или он не открылся

    // Полная загрузка (хвост не больше kMaxReadBytes) или дозагрузка
    // дописанных байт: журнал пишется только дополнением, поэтому
    // при росте файла читаются только новые записи
    const bool appendOnly = recPath == task.path && recSize >= 0
        && outcome.size > recSize && outcome.size >= recEnd;
    qint64 from = 0;
    if (appendOnly) {
        from = recEnd;
    } else {
        records.clear();
        if (outcome.size > kMaxReadBytes)
            from = outcome.size - kMaxReadBytes;   // читаем только хвост
    }
    if (from < outcome.size && file.seek(from)) {
        const QByteArray buffer = file.read(outcome.size - from);
        qint64 base = from;
        // При полной загрузке с хвоста файла начало чтения может попасть
        // посередине строки — такую неполную первую строку отбрасываем;
        // при дозагрузке from указывает на границу строки — ничего не теряем
        if (from > 0 && !appendOnly) {
            const qsizetype nl = buffer.indexOf('\n');
            if (nl < 0)
                return outcome;   // весь кусок — одна неполная строка
            base = from + nl + 1;
        }
        const qint64 consumed = parseJournalLines(buffer.constData() + (base - from),
                                                  buffer.size() - (base - from),
                                                  records);
        recEnd = base + consumed;
    }
    recPath = task.path;
    recSize = outcome.size;
    recMtime = outcome.mtime;

    outcome.ok = true;
    outcome.records = records.size();
    outcome.result = PacketGrouper::groupRecords(records, task.fields);
    return outcome;
}

void GroupingWidget::applyJournalOutcome(const JournalOutcome &outcome)
{
    m_loadedFile = outcome.absolutePath;
    m_loadedFileSize = outcome.size;
    m_loadedFileMtime = outcome.mtime;
    m_lastJournalMs = QDateTime::currentMSecsSinceEpoch();

    if (!outcome.ok) {
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        m_summaryLabel->setText(
            tr("Не удалось прочитать журнал %1.").arg(outcome.fileName));
        m_detail->clear();
        return;
    }
    if (outcome.records == 0) {
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        m_summaryLabel->setText(
            tr("В файле %1 нет записей пакетов.").arg(outcome.fileName));
        m_detail->clear();
        return;
    }
    fillTable(outcome.result);
    m_summaryLabel->setText(
        tr("Групп: %1, пакетов: %2, байт: %3 — источник: журнал %4 (%5 записей)")
            .arg(outcome.result.groups.size())
            .arg(outcome.result.packets)
            .arg(formatBytes(outcome.result.bytes))
            .arg(outcome.fileName)
            .arg(outcome.records));
}

GroupingWidget::~GroupingWidget()
{
    // Дождаться текущей задачи журнала (обычно поток уже завершён)
    if (m_worker.joinable())
        m_worker.join();
}

void GroupingWidget::fillTable(const PacketGrouper::Result &result)
{
    const QVector<FilterCondition::Field> fields = selectedFields();

    // Запомнить выбранную группу (ключи), чтобы выделение пережило пересчёт
    const QStringList selectedKeys = selectedGroupKeys();

    QStringList headers;
    for (const FilterCondition::Field field : fields)
        headers << FilterCondition::fieldName(field);
    headers << tr("Пакетов") << tr("Байт") << tr("Ср. размер")
            << tr("Пакетов, %") << tr("Байт, %") << tr("Период");

    // Заполнение без перерисовки до готовности: иначе таблица
    // перерисовывается на каждой вставленной ячейке
    m_table->setUpdatesEnabled(false);
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    const int keyCols = fields.size();
    QHeaderView *header = m_table->horizontalHeader();
    for (int c = 0; c < keyCols; ++c)
        header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // Числовые колонки — фиксированная ширина: пересчёт «по содержимому»
    // при тысячах строк сканирует всю таблицу и замедляет каждый пересчёт
    static const int kNumericWidths[] = { 80, 100, 100, 70, 70 };
    for (int i = 0; i < 5; ++i) {
        const int c = keyCols + i;
        if (c >= headers.size())
            break;
        header->setSectionResizeMode(c, QHeaderView::Interactive);
        header->resizeSection(c, kNumericWidths[i]);
    }
    header->setStretchLastSection(true);

    const int shown = qMin(result.groups.size(), kMaxGroupsShown);
    m_table->setRowCount(shown + (result.groups.size() > shown ? 1 : 0));

    int row = 0;
    for (int g = 0; g < shown; ++g) {
        const PacketGrouper::GroupStats &stats = result.groups.at(g);
        const double packetsPct = result.packets > 0
            ? 100.0 * stats.packets / result.packets : 0.0;
        const double bytesPct = result.bytes > 0
            ? 100.0 * stats.bytes / result.bytes : 0.0;
        const double avg = stats.packets > 0 ? double(stats.bytes) / stats.packets : 0.0;

        QString period;
        if (stats.firstMs > 0 && stats.lastMs > stats.firstMs)
            period = QStringLiteral("%1 — %2")
                         .arg(formatTimeMs(stats.firstMs), formatTimeMs(stats.lastMs));
        else if (stats.firstMs > 0)
            period = formatTimeMs(stats.firstMs);

        // Подробная сводка группы (для нижней панели)
        QStringList detailLines;
        for (int c = 0; c < keyCols; ++c)
            detailLines << QStringLiteral("%1: %2")
                               .arg(headers.at(c), stats.keys.value(c));
        detailLines << QStringLiteral("Пакетов: %1 (%2%)")
                           .arg(stats.packets)
                           .arg(packetsPct, 0, 'f', 1);
        detailLines << QStringLiteral("Байт: %1 (%2%)")
                           .arg(formatBytes(stats.bytes))
                           .arg(bytesPct, 0, 'f', 1);
        detailLines << QStringLiteral("Средний размер пакета: %1")
                           .arg(formatBytes(qint64(avg)));
        if (!period.isEmpty())
            detailLines << QStringLiteral("Период наблюдения: %1").arg(period);
        const QString detail = detailLines.join(QLatin1Char('\n'));

        for (int c = 0; c < keyCols; ++c) {
            auto *item = new QTableWidgetItem(stats.keys.value(c));
            if (c == 0) {
                item->setData(Qt::UserRole, detail);         // сводка — в первой ячейке
                item->setData(Qt::UserRole + 2, stats.keys); // ключи — для восстановления выделения
            }
            m_table->setItem(row, c, item);
        }
        // Числовые колонки: отформатированное значение + исходное число
        // в UserRole+1 (для возможной ручной сортировки)
        auto makeNumberItem = [](const QString &text, double raw) {
            auto *item = new QTableWidgetItem(text);
            item->setData(Qt::UserRole + 1, raw);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return item;
        };
        m_table->setItem(row, keyCols,
                         makeNumberItem(QString::number(stats.packets), stats.packets));
        m_table->setItem(row, keyCols + 1,
                         makeNumberItem(formatBytes(stats.bytes), stats.bytes));
        m_table->setItem(row, keyCols + 2,
                         makeNumberItem(formatBytes(qint64(avg)), avg));
        m_table->setItem(row, keyCols + 3,
                         makeNumberItem(QStringLiteral("%1").arg(packetsPct, 0, 'f', 1),
                                        packetsPct));
        m_table->setItem(row, keyCols + 4,
                         makeNumberItem(QStringLiteral("%1").arg(bytesPct, 0, 'f', 1),
                                        bytesPct));
        m_table->setItem(row, keyCols + 5,
                         makeNumberItem(period,
                                        double(stats.lastMs > 0 ? stats.lastMs
                                                                : stats.firstMs)));
        ++row;
    }
    m_limitRows = 0;
    if (result.groups.size() > shown) {
        m_table->setItem(shown, 0, new QTableWidgetItem(
            tr("… и ещё %1 групп (показаны первые %2)")
                .arg(result.groups.size() - shown)
                .arg(shown)));
        m_limitRows = 1;
    }

    // Сортировка: по умолчанию — по числу пакетов (убывание, как в ядре);
    // порядок, выбранный кликом по заголовку, переносится на новое построение
    m_table->setSortingEnabled(false);
    if (!m_sortInitialized && keyCols < m_table->columnCount()) {
        m_sortColumn = keyCols;
        m_sortOrder = Qt::DescendingOrder;
        m_sortInitialized = true;
    }
    if (m_sortColumn < 0 || m_sortColumn >= m_table->columnCount()) {
        m_sortColumn = keyCols < m_table->columnCount() ? keyCols : 0;
        m_sortOrder = Qt::DescendingOrder;
    }
    applySort();

    // Восстановить выделение группы после пересчёта (панель подробностей
    // и выделение строки переживают обновление по таймеру)
    restoreSelection(selectedKeys);
    m_table->setUpdatesEnabled(true);
}

QStringList GroupingWidget::selectedGroupKeys() const
{
    const int row = m_table->currentRow();
    const QTableWidgetItem *item = row >= 0 ? m_table->item(row, 0) : nullptr;
    return item ? item->data(Qt::UserRole + 2).toStringList() : QStringList();
}

void GroupingWidget::restoreSelection(const QStringList &keys)
{
    if (keys.isEmpty())
        return;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem *item = m_table->item(row, 0);
        if (item && item->data(Qt::UserRole + 2).toStringList() == keys) {
            m_table->setCurrentCell(row, 0);
            m_table->selectRow(row);
            return;
        }
    }
}

void GroupingWidget::applySort()
{
    if (m_sortColumn < 0 || m_sortColumn >= m_table->columnCount())
        return;
    TableSorter::sortRows(m_table, m_sortColumn, m_sortOrder, m_limitRows);
    // setSortingEnabled(false) в fillTable гасит индикатор — возвращаем его
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    m_table->horizontalHeader()->setSortIndicator(m_sortColumn, m_sortOrder);
}

void GroupingWidget::saveState()
{
    QSettings s = Settings::openSettings();
    s.setValue(QStringLiteral("grouping/sourceLog"), m_sourceLog->isChecked());
    s.setValue(QStringLiteral("grouping/field1"), m_field1->currentData().toInt());
    s.setValue(QStringLiteral("grouping/field2"), m_field2->currentData().toInt());
    s.setValue(QStringLiteral("grouping/field3"), m_field3->currentData().toInt());
    s.setValue(QStringLiteral("grouping/logFile"), m_logCombo->currentData().toString());
    s.setValue(QStringLiteral("grouping/sortColumn"),
               m_sortInitialized ? m_sortColumn : -1);
    s.setValue(QStringLiteral("grouping/sortOrder"), int(m_sortOrder));
}

void GroupingWidget::restoreState()
{
    QSettings s = Settings::openSettings();
    const bool useLog = s.value(QStringLiteral("grouping/sourceLog"), false).toBool();
    m_sourceLog->setChecked(useLog);
    m_sourceStream->setChecked(!useLog);
    const int f1 = s.value(QStringLiteral("grouping/field1"),
                           static_cast<int>(FilterCondition::Field::SrcIp)).toInt();
    const int f2 = s.value(QStringLiteral("grouping/field2"),
                           static_cast<int>(FilterCondition::Field::DstIp)).toInt();
    const int f3 = s.value(QStringLiteral("grouping/field3"), kNoField).toInt();
    m_field1->setCurrentIndex(m_field1->findData(f1));
    m_field2->setCurrentIndex(m_field2->findData(f2));
    m_field3->setCurrentIndex(m_field3->findData(f3));
    // Порядок сортировки таблицы групп
    m_sortColumn = s.value(QStringLiteral("grouping/sortColumn"), -1).toInt();
    const int order = s.value(QStringLiteral("grouping/sortOrder"),
                              int(Qt::DescendingOrder)).toInt();
    m_sortOrder = order == int(Qt::AscendingOrder)
        ? Qt::AscendingOrder : Qt::DescendingOrder;
    m_sortInitialized = m_sortColumn >= 0;
}
