#include "PayloadSignatureRule.h"

#include <QDateTime>

PayloadSignatureRule::PayloadSignatureRule(const DetectionSettings *settings)
    : AnomalyRule(settings)
{
}

std::optional<Anomaly> PayloadSignatureRule::check(const Packet &packet)
{
    if (!settings().enablePayload)
        return std::nullopt;
    const QByteArray &payload = packet.payload();
    if (payload.isEmpty())
        return std::nullopt;

    const QString haystack = QString::fromLatin1(payload).toLower();
    if (haystack.isEmpty())
        return std::nullopt;

    for (const QString &rawSignature : settings().payloadSignatures) {
        const QString signature = rawSignature.trimmed().toLower();
        if (signature.isEmpty() || !haystack.contains(signature))
            continue;

        const qint64 now = packet.timestamp().toMSecsSinceEpoch();
        const QString key = packet.sourceIp() + QLatin1Char('|') +
                            packet.destinationIp() + QLatin1Char('|') + signature;
        // Срабатывание по той же паре хостов — не чаще cooldown.
        const qint64 cooldownMs = static_cast<qint64>(settings().anomalyCooldownSec) * 1000;
        const auto it = m_lastEmit.constFind(key);
        if (it != m_lastEmit.constEnd() && now - it.value() < cooldownMs)
            continue;
        m_lastEmit.insert(key, now);
        if (m_lastEmit.size() > 512)
            m_lastEmit.clear();

        Anomaly anomaly = makeAnomaly(packet, Anomaly::Severity::Critical,
                                      QStringLiteral("Сработала сигнатура «%1»").arg(rawSignature),
                                      QStringLiteral("В полезной нагрузке пакета %1 → %2 найдена "
                                                     "подозрительная строка «%3» (контекст: %4)")
                                          .arg(packet.sourceIp(), packet.destinationIp(),
                                               rawSignature, packet.payloadPreview(64)));
        anomaly.setSourceIp(packet.sourceIp());
        anomaly.setDestinationIp(packet.destinationIp());
        anomaly.setSourcePort(packet.sourcePort());
        anomaly.setDestinationPort(packet.destinationPort());
        anomaly.setSample(packet.payloadPreview(80));
        return anomaly;
    }
    return std::nullopt;
}

void PayloadSignatureRule::reset()
{
    m_lastEmit.clear();
}
