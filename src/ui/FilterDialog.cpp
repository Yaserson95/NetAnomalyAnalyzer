#include "FilterDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
/** Действия, доступные для типа поля. */
QList<FilterCondition::Op> opsFor(bool numeric)
{
    if (numeric)
        return { FilterCondition::Op::Equal, FilterCondition::Op::NotEqual,
                 FilterCondition::Op::Greater, FilterCondition::Op::Less };
    return { FilterCondition::Op::Contains, FilterCondition::Op::NotContains,
             FilterCondition::Op::Equal,    FilterCondition::Op::NotEqual,
             FilterCondition::Op::StartsWith, FilterCondition::Op::EndsWith,
             FilterCondition::Op::Regex };
}
} // namespace

FilterDialog::FilterDialog(const QVector<FilterCondition> &conditions,
                           const QVector<FilterCondition::Field> &fields,
                           QWidget *parent)
    : QDialog(parent)
    , m_fields(fields)
{
    setWindowTitle(tr("Фильтр — параметр, действие, значение"));
    resize(640, 320);

    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Составьте условия вида «параметр — действие — значение», "
           "например: MAC отправителя | начинается на | bc:24. "
           "Несколько условий объединяются по «И» (все должны выполняться)."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#666"));
    layout->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("Параметр"), tr("Действие"), tr("Значение") });
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_table, 1);

    // Кнопки управления условиями
    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Добавить условие"), this);
    auto *delBtn = new QPushButton(tr("Удалить выбранное"), this);
    auto *clearBtn = new QPushButton(tr("Очистить все"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch(1);
    layout->addLayout(btnRow);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Применить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    layout->addWidget(buttons);

    for (const FilterCondition &condition : conditions)
        appendRow(condition);

    connect(addBtn, &QPushButton::clicked, this, &FilterDialog::addCondition);
    connect(delBtn, &QPushButton::clicked, this, &FilterDialog::removeSelected);
    connect(clearBtn, &QPushButton::clicked, this, &FilterDialog::clearAll);
    connect(buttons, &QDialogButtonBox::accepted, this, &FilterDialog::onAccepted);
    connect(buttons, &QDialogButtonBox::rejected, this, &FilterDialog::reject);
}

QVector<FilterCondition> FilterDialog::conditions() const
{
    QVector<FilterCondition> result;
    result.reserve(m_table->rowCount());
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *fieldCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, 0));
        auto *opCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, 1));
        auto *valueEdit = qobject_cast<QLineEdit *>(m_table->cellWidget(row, 2));
        if (!fieldCombo || !opCombo || !valueEdit)
            continue;
        result.append({ static_cast<FilterCondition::Field>(
                            fieldCombo->currentData().toInt()),
                        static_cast<FilterCondition::Op>(opCombo->currentData().toInt()),
                        valueEdit->text().trimmed() });
    }
    return result;
}

void FilterDialog::addCondition()
{
    if (!m_fields.isEmpty())
        appendRow(FilterCondition(m_fields.first(), FilterCondition::Op::Contains, {}));
}

void FilterDialog::removeSelected()
{
    const int row = selectedRow();
    if (row >= 0)
        m_table->removeRow(row);
}

void FilterDialog::clearAll()
{
    m_table->setRowCount(0);
}

void FilterDialog::onAccepted()
{
    // Проверка: значение обязательно, regex — корректный
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *opCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, 1));
        auto *valueEdit = qobject_cast<QLineEdit *>(m_table->cellWidget(row, 2));
        if (!opCombo || !valueEdit)
            continue;
        const QString value = valueEdit->text().trimmed();
        if (value.isEmpty()) {
            QMessageBox::warning(this, tr("Некорректный фильтр"),
                                 tr("Строка %1: не задано значение фильтра.").arg(row + 1));
            return;
        }
        const auto op = static_cast<FilterCondition::Op>(opCombo->currentData().toInt());
        if (op == FilterCondition::Op::Regex &&
            !QRegularExpression(value).isValid()) {
            QMessageBox::warning(this, tr("Некорректный фильтр"),
                                 tr("Строка %1: «%2» — недопустимое регулярное выражение.")
                                     .arg(row + 1)
                                     .arg(value));
            return;
        }
    }
    accept();
}

void FilterDialog::appendRow(const FilterCondition &condition)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    // Параметр
    auto *fieldCombo = new QComboBox(m_table);
    int selectedFieldIndex = 0;
    for (int i = 0; i < m_fields.size(); ++i) {
        const auto field = m_fields.at(i);
        fieldCombo->addItem(FilterCondition::fieldName(field), static_cast<int>(field));
        if (field == condition.field)
            selectedFieldIndex = i;
    }
    fieldCombo->setCurrentIndex(selectedFieldIndex);
    m_table->setCellWidget(row, 0, fieldCombo);

    // Действие (список зависит от типа параметра)
    auto *opCombo = new QComboBox(m_table);
    m_table->setCellWidget(row, 1, opCombo);
    auto fillOps = [opCombo](const FilterCondition::Field &field,
                             FilterCondition::Op selected) {
        opCombo->clear();
        const auto ops = opsFor(FilterCondition::isNumeric(field));
        int sel = 0;
        for (int i = 0; i < ops.size(); ++i) {
            opCombo->addItem(FilterCondition::opName(ops.at(i)),
                             static_cast<int>(ops.at(i)));
            if (ops.at(i) == selected)
                sel = i;
        }
        opCombo->setCurrentIndex(sel);
    };
    fillOps(m_fields.value(selectedFieldIndex), condition.op);
    connect(fieldCombo, &QComboBox::currentIndexChanged, this,
            [this, opCombo, fillOps](int index) {
                const auto field = m_fields.value(index, m_fields.value(0));
                const auto op = static_cast<FilterCondition::Op>(
                    opCombo->currentData().toInt());
                fillOps(field, op);
            });

    // Значение
    auto *valueEdit = new QLineEdit(condition.value, m_table);
    valueEdit->setPlaceholderText(tr("значение…"));
    m_table->setCellWidget(row, 2, valueEdit);
}

int FilterDialog::selectedRow() const
{
    return m_table->currentRow();
}
