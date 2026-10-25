#pragma once

#include "../core/FilterCondition.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QTableWidget;
class QLineEdit;

/**
 * @brief Диалог составления фильтра из условий вида
 * «параметр — действие — значение» (например,
 * «MAC отправителя | начинается на | bc:24»).
 *
 * Несколько условий объединяются по «И». Список доступных параметров
 * задаётся вызывающей стороной (пакеты или записи журналов).
 */
class FilterDialog : public QDialog {
    Q_OBJECT
public:
    FilterDialog(const QVector<FilterCondition> &conditions,
                 const QVector<FilterCondition::Field> &fields,
                 QWidget *parent = nullptr);

    /** Условия, составленные пользователем. */
    QVector<FilterCondition> conditions() const;

private slots:
    void addCondition();
    void removeSelected();
    void clearAll();
    void onAccepted();

private:
    /** Строка таблицы: Параметр + Действие + Значение. */
    void appendRow(const FilterCondition &condition);
    int  selectedRow() const;

    QVector<FilterCondition::Field> m_fields;
    QTableWidget *m_table = nullptr;
};
