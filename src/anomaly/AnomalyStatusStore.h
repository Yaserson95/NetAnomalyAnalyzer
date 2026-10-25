#pragma once

#include <QJsonObject>
#include <QString>

/**
 * @brief Хранилище статусов аномалий («обнаружено» / «решено»).
 *
 * Аномалии пишутся в anomalies.jsonl только в момент обнаружения
 * (журнал неизменяем), а отметки оператора хранятся отдельно — в файле
 * anomaly-status.json по пути <логи>/anomaly-status.json.
 *
 * Ключ записи — «id@timestamp»: идентификаторы аномалий уникальны
 * только внутри запуска приложения, поэтому одного id недостаточно.
 */
class AnomalyStatusStore {
public:
    static const char *kDetected;   ///< «обнаружено» (по умолчанию)
    static const char *kResolved;   ///< «решено» (отметка оператора)

    AnomalyStatusStore() = default;
    explicit AnomalyStatusStore(const QString &filePath);

    /** Задать файл состояния и загрузить его. */
    void setFilePath(const QString &filePath);

    /** Ключ аномалии: «id@timestamp» (ISO-время как в журнале). */
    static QString keyFor(qint64 id, const QString &timestampIso);
    static QString keyFor(const QJsonObject &record);

    /** Статус: detected / resolved. */
    QString status(const QString &key) const;
    bool isResolved(const QString &key) const;
    /** ISO-время отметки «решено» (пусто, если не отмечалась). */
    QString resolvedAt(const QString &key) const;

    /** Установить/снять отметку «решено» (сразу сохраняет файл). */
    void setResolved(const QString &key, bool resolved);

    int trackedCount() const;

private:
    void load();
    void save() const;

    QString    m_filePath;
    QJsonObject m_statusMap;   ///< ключ → {"status":…, "resolvedAt":…}
};
