#include "TableSorter.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QVariant>

#include <algorithm>
#include <numeric>

namespace {
/** Ключ сортировки одной ячейки. */
struct RowKey {
    bool    hasNumber = false;
    double  number    = 0.0;
    QString text;
};

RowKey cellKey(const QTableWidget *table, int row, int column)
{
    RowKey key;
    const QTableWidgetItem *item = table->item(row, column);
    if (!item)
        return key;
    key.text = item->text();
    const QVariant value = item->data(Qt::UserRole + 1);
    if (value.isValid()) {
        key.hasNumber = true;
        key.number = value.toDouble();
    }
    return key;
}
} // namespace

bool TableSorter::columnIsNumeric(const QTableWidget *table, int column)
{
    if (!table || column < 0 || column >= table->columnCount())
        return false;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, column);
        if (item && item->data(Qt::UserRole + 1).isValid())
            return true;
    }
    return false;
}

void TableSorter::sortRows(QTableWidget *table, int column, Qt::SortOrder order,
                           int pinnedLastRows)
{
    if (!table || column < 0 || column >= table->columnCount())
        return;
    const int rows = table->rowCount();
    const int cols = table->columnCount();
    if (rows <= 1)
        return;
    const int sortableRows =
        std::clamp(rows - std::clamp(pinnedLastRows, 0, rows), 0, rows);

    // Ключи — до отсоединения элементов
    QVector<RowKey> keys(rows);
    for (int row = 0; row < rows; ++row)
        keys[row] = cellKey(table, row, column);

    QVector<int> orderIndex(sortableRows);
    std::iota(orderIndex.begin(), orderIndex.end(), 0);
    const bool descending = (order == Qt::DescendingOrder);
    std::sort(orderIndex.begin(), orderIndex.end(), [&](int a, int b) {
        const RowKey &ka = keys[a];
        const RowKey &kb = keys[b];
        int cmp = 0;
        if (ka.hasNumber && kb.hasNumber)
            cmp = (ka.number == kb.number) ? 0 : (ka.number < kb.number ? -1 : 1);
        else
            cmp = QString::localeAwareCompare(ka.text, kb.text);
        if (cmp == 0)
            return a < b;   // равные ключи — исходный порядок
        return descending ? cmp > 0 : cmp < 0;
    });

    // Таблица уже в нужном порядке (например, заполнена по убыванию числа
    // пакетов, как в ядре) — перекладка десятков тысяч ячеек не нужна
    bool inOrder = true;
    for (int row = 0; row < sortableRows; ++row) {
        if (orderIndex[row] != row) {
            inOrder = false;
            break;
        }
    }
    if (inOrder)
        return;

    // Отсоединяем элементы: владение переходит нам, строки остаются
    QVector<QVector<QTableWidgetItem *>> cells(rows);
    for (int row = 0; row < rows; ++row) {
        cells[row].resize(cols);
        for (int col = 0; col < cols; ++col)
            cells[row][col] = table->takeItem(row, col);
    }

    for (int row = 0; row < sortableRows; ++row)
        for (int col = 0; col < cols; ++col)
            table->setItem(row, col, cells[orderIndex[row]][col]);
    // Строки-ограничители возвращаются на свои места внизу
    for (int row = sortableRows; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            table->setItem(row, col, cells[row][col]);
}
