#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

/**
 * @brief Информация об обнаруженной аномалии.
 */
class Anomaly {
public:
    enum class Severity { Info, Warning, Critical };

    Anomaly() = default;

    static QString severityName(Severity severity);
    QString severityName() const { return severityName(m_severity); }

    qint64 id() const { return m_id; }
    void setId(qint64 id) { m_id = id; }

    const QDateTime &timestamp() const { return m_timestamp; }
    void setTimestamp(const QDateTime &ts) { m_timestamp = ts; }

    const QString &interfaceName() const { return m_interfaceName; }
    void setInterfaceName(const QString &name) { m_interfaceName = name; }

    const QString &ruleName() const { return m_ruleName; }
    void setRuleName(const QString &name) { m_ruleName = name; }

    /** Кто выявил аномалию: встроенный анализатор или название модели LLM.
     *  Пусто = встроенный анализатор (см. defaultDetectorName()). */
    const QString &detector() const { return m_detector; }
    void setDetector(const QString &detector) { m_detector = detector; }
    /** Отображаемое имя встроенного анализатора. */
    static QString defaultDetectorName();

    Severity severity() const { return m_severity; }
    void setSeverity(Severity severity) { m_severity = severity; }

    const QString &title() const { return m_title; }
    void setTitle(const QString &title) { m_title = title; }

    const QString &description() const { return m_description; }
    void setDescription(const QString &text) { m_description = text; }

    const QString &sourceIp() const { return m_sourceIp; }
    void setSourceIp(const QString &ip) { m_sourceIp = ip; }

    const QString &destinationIp() const { return m_destinationIp; }
    void setDestinationIp(const QString &ip) { m_destinationIp = ip; }

    quint16 sourcePort() const { return m_sourcePort; }
    void setSourcePort(quint16 port) { m_sourcePort = port; }

    quint16 destinationPort() const { return m_destinationPort; }
    void setDestinationPort(quint16 port) { m_destinationPort = port; }

    int packetCount() const { return m_packetCount; }
    void setPacketCount(int count) { m_packetCount = count; }

    const QString &sample() const { return m_sample; }
    void setSample(const QString &sample) { m_sample = sample; }

    /** Однострочное сообщение для оповещений. */
    QString notificationText() const;
    QJsonObject toJson() const;
    /** Восстановить аномалию из JSON-записи журнала (для загрузки истории). */
    static Anomaly fromJson(const QJsonObject &object);

private:
    qint64    m_id = 0;
    QDateTime m_timestamp;
    QString   m_interfaceName;
    QString   m_ruleName;
    QString   m_detector;          ///< кто выявил (пусто = анализатор)
    Severity  m_severity = Severity::Warning;
    QString   m_title;
    QString   m_description;
    QString   m_sourceIp;
    QString   m_destinationIp;
    quint16   m_sourcePort = 0;
    quint16   m_destinationPort = 0;
    int       m_packetCount = 1;
    QString   m_sample;
};

Q_DECLARE_METATYPE(Anomaly)
