#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

/**
 * @brief Учёт аномалий, уже отправленных в LLM.
 *
 * Контекст анализа должен содержать только новые аномалии: каждая запись
 * уходит в LLM один раз, при последующих запросах (по расписанию или
 * вручную) ранее отправленные исключаются из промпта. Ключ — «id@timestamp»
 * (AnomalyStatusStore::keyFor), отметки хранятся в <логи>/llm-sent.json
 * в виде массива ключей в порядке отправки и переживают перезапуск.
 *
 * Отметка ставится только после успешного ответа: если запрос завершился
 * ошибкой, аномалии в 다음 раз отправляются повторно.
 */
class LlmSentStore {
public:
    /** Сколько ключей хранить (старые вытесняются). */
    static constexpr int kMaxKeys = 5000;

    LlmSentStore() = default;
    explicit LlmSentStore(const QString &filePath);

    /** Задать файл состояния и загрузить его. */
    void setFilePath(const QString &filePath);

    /** Аномалия уже отправлялась в LLM? */
    bool contains(const QString &key) const { return m_keySet.contains(key); }
    int size() const { return m_keys.size(); }

    /** Отметить аномалии отправленными (сразу сохраняет файл).
     *  @return число действительно новых отметок. */
    int markSent(const QStringList &keys);

private:
    void load();
    void save() const;

    QString       m_filePath;
    QStringList   m_keys;     ///< в порядке отправки (для вытеснения старых)
    QSet<QString> m_keySet;   ///< те же ключи — для поиска
};
