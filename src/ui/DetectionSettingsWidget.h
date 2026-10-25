#pragma once

#include <QWidget>

class QCheckBox;
class QSpinBox;
class QLineEdit;
class QTextEdit;

/**
 * @brief Настройки правил обнаружения аномалий (пороги, порты, сигнатуры).
 */
class DetectionSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit DetectionSettingsWidget(QWidget *parent = nullptr);

    void load();
    void apply();

private:
    QCheckBox *m_portScanCheck = nullptr;
    QCheckBox *m_synFloodCheck = nullptr;
    QCheckBox *m_udpFloodCheck = nullptr;
    QCheckBox *m_icmpFloodCheck = nullptr;
    QCheckBox *m_broadcastCheck = nullptr;
    QCheckBox *m_suspiciousPortCheck = nullptr;
    QCheckBox *m_payloadCheck = nullptr;

    QSpinBox  *m_portScanThreshold = nullptr;
    QSpinBox  *m_portScanWindow = nullptr;
    QSpinBox  *m_synFloodPps = nullptr;
    QSpinBox  *m_udpFloodPps = nullptr;
    QSpinBox  *m_icmpFloodPps = nullptr;
    QSpinBox  *m_broadcastPps = nullptr;
    QSpinBox  *m_cooldown = nullptr;

    QLineEdit *m_portsEdit = nullptr;
    QTextEdit *m_signaturesEdit = nullptr;
};
