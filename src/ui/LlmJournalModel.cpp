#include "LlmJournalModel.h"

#include <QColor>

LlmJournalModel::LlmJournalModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void LlmJournalModel::setJournal(LlmJournal *journal)
{
    beginResetModel();
    if (m_journal)
        disconnect(m_journal, nullptr, this, nullptr);
    m_journal = journal;
    if (m_journal) {
        connect(m_journal, &LlmJournal::entryAdded, this, &LlmJournalModel::onEntryAdded);
        connect(m_journal, &LlmJournal::entryUpdated, this, &LlmJournalModel::onEntryUpdated);
        connect(m_journal, &LlmJournal::modelReset, this, &LlmJournalModel::onReset);
    }
    endResetModel();
}

int LlmJournalModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_journal ? 0 : m_journal->entries().size();
}

int LlmJournalModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LlmJournalModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColTime:      return tr("Время");
    case ColSource:    return tr("Источник");
    case ColStatus:    return tr("Статус");
    case ColVerdict:   return tr("Вердикт LLM");
    case ColModel:     return tr("Модель");
    case ColDuration:  return tr("Длительность");
    case ColAnomalies: return tr("Аномалий в запросе");
    case ColResponse:  return tr("Ответ (кратко)");
    }
    return {};
}

QVariant LlmJournalModel::data(const QModelIndex &index, int role) const
{
    if (!m_journal || !index.isValid() || index.row() >= m_journal->entries().size())
        return {};
    const LlmJournalEntry &entry = m_journal->entries().at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColTime:
            return entry.startTime.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
        case ColSource:    return LlmJournalEntry::sourceName(entry.source);
        case ColStatus:    return LlmJournalEntry::statusName(entry.status);
        case ColVerdict:   return LlmJournalEntry::verdictName(entry.verdictStatus);
        case ColModel:     return entry.model;
        case ColDuration:
            return entry.status == LlmJournalEntry::Status::Pending
                       ? QStringLiteral("…")
                       : QStringLiteral("%1 с").arg(entry.durationSec());
        case ColAnomalies: return entry.anomaliesInPrompt;
        case ColResponse:
            if (entry.status == LlmJournalEntry::Status::Error)
                return entry.error;
            if (entry.status == LlmJournalEntry::Status::Pending)
                return tr("выполняется…");
            return entry.shortResponse();
        }
    } else if (role == Qt::ForegroundRole) {
        // Колонка вердикта окрашивается по уровню ответа LLM
        if (index.column() == ColVerdict) {
            if (entry.verdictStatus == QLatin1String("critical"))
                return QColor(192, 57, 43);
            if (entry.verdictStatus == QLatin1String("warning"))
                return QColor(214, 137, 16);
            if (entry.verdictStatus == QLatin1String("ok"))
                return QColor(39, 125, 56);
            return QColor(127, 140, 141);
        }
        if (entry.status == LlmJournalEntry::Status::Error)
            return QColor(192, 57, 43);
        if (entry.status == LlmJournalEntry::Status::Pending)
            return QColor(41, 128, 185);
        return QColor(39, 125, 56);
    } else if (role == Qt::ToolTipRole) {
        if (index.column() == ColVerdict && !entry.verdictStatus.isEmpty())
            return entry.verdictStatus;
        if (entry.status == LlmJournalEntry::Status::Error)
            return entry.error;
        return entry.response;
    }
    return {};
}

const LlmJournalEntry *LlmJournalModel::entryAt(int row) const
{
    if (!m_journal || row < 0 || row >= m_journal->entries().size())
        return nullptr;
    return &m_journal->entries().at(row);
}

void LlmJournalModel::onEntryAdded(qint64)
{
    if (!m_journal)
        return;
    const int row = m_journal->entries().size() - 1;
    if (row >= 0) {
        beginInsertRows(QModelIndex(), row, row);
        endInsertRows();
    }
}

void LlmJournalModel::onEntryUpdated(qint64)
{
    if (!m_journal)
        return;
    emit dataChanged(index(0, 0),
                     index(m_journal->entries().size() - 1, ColumnCount - 1));
}

void LlmJournalModel::onReset()
{
    beginResetModel();
    endResetModel();
}
