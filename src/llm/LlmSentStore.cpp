#include "LlmSentStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

LlmSentStore::LlmSentStore(const QString &filePath)
{
    setFilePath(filePath);
}

void LlmSentStore::setFilePath(const QString &filePath)
{
    m_filePath = filePath;
    load();
}

int LlmSentStore::markSent(const QStringList &keys)
{
    int added = 0;
    for (const QString &key : keys) {
        if (m_keySet.contains(key))
            continue;
        m_keySet.insert(key);
        m_keys.append(key);
        ++added;
    }

    // Вытеснение самых старых отметок — растёт только при активном потоке
    // аномалий; файл не должен разрастаться бесконечно.
    if (m_keys.size() > kMaxKeys) {
        const int excess = m_keys.size() - kMaxKeys;
        for (int i = 0; i < excess; ++i)
            m_keySet.remove(m_keys.at(i));
        m_keys.remove(0, excess);
    }

    if (added > 0)
        save();
    return added;
}

void LlmSentStore::load()
{
    m_keys.clear();
    m_keySet.clear();

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        const QString key = value.toString();
        if (key.isEmpty() || m_keySet.contains(key))
            continue;
        m_keys.append(key);
        m_keySet.insert(key);
    }
    // Подрезка без пересохранения: файл поправится при следующей отметке.
    if (m_keys.size() > kMaxKeys) {
        const int excess = m_keys.size() - kMaxKeys;
        for (int i = 0; i < excess; ++i)
            m_keySet.remove(m_keys.at(i));
        m_keys.remove(0, excess);
    }
}

void LlmSentStore::save() const
{
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonArray array;
    for (const QString &key : m_keys)
        array.append(key);
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.commit();
}
