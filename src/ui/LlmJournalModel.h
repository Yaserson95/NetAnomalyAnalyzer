#pragma once

#include "../llm/LlmJournal.h"

#include <QAbstractTableModel>

/**
 * @brief Модель журнала обращений к LLM.
 */
class LlmJournalModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColTime, ColSource, ColStatus, ColVerdict, ColModel,
                  ColDuration, ColAnomalies, ColResponse, ColumnCount };

    explicit LlmJournalModel(QObject *parent = nullptr);

    void setJournal(LlmJournal *journal);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    const LlmJournalEntry *entryAt(int row) const;

private slots:
    void onEntryAdded(qint64 id);
    void onEntryUpdated(qint64 id);
    void onReset();

private:
    LlmJournal *m_journal = nullptr;
};
