#include "AnomalyRule.h"

#include "BroadcastStormRule.h"
#include "IcmpFloodRule.h"
#include "PayloadSignatureRule.h"
#include "PortScanRule.h"
#include "SuspiciousPortRule.h"
#include "SynFloodRule.h"
#include "UdpFloodRule.h"

AnomalyRule::AnomalyRule(const DetectionSettings *settings)
    : m_settings(settings)
{
}

Anomaly AnomalyRule::makeAnomaly(const Packet &packet, Anomaly::Severity severity,
                                 const QString &title, const QString &description) const
{
    Anomaly anomaly;
    anomaly.setTimestamp(packet.timestamp());
    anomaly.setInterfaceName(packet.interfaceName());
    anomaly.setRuleName(name());
    anomaly.setSeverity(severity);
    anomaly.setTitle(title);
    anomaly.setDescription(description);
    anomaly.setSample(packet.summary());
    anomaly.setPacketCount(1);
    return anomaly;
}

std::vector<std::unique_ptr<AnomalyRule>> createDefaultRules(const DetectionSettings *settings)
{
    std::vector<std::unique_ptr<AnomalyRule>> rules;
    rules.push_back(std::make_unique<PortScanRule>(settings));
    rules.push_back(std::make_unique<SynFloodRule>(settings));
    rules.push_back(std::make_unique<UdpFloodRule>(settings));
    rules.push_back(std::make_unique<IcmpFloodRule>(settings));
    rules.push_back(std::make_unique<BroadcastStormRule>(settings));
    rules.push_back(std::make_unique<SuspiciousPortRule>(settings));
    rules.push_back(std::make_unique<PayloadSignatureRule>(settings));
    return rules;
}
