#pragma once

#include <QWidget>

class QCheckBox;
class QSpinBox;
class QLineEdit;

/**
 * @brief Настройки журналирования: каталог, ротация, логирование пакетов.
 */
class LoggingSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit LoggingSettingsWidget(QWidget *parent = nullptr);

    void load();
    void apply();

private:
    QCheckBox *m_logPacketsCheck = nullptr;
    QLineEdit *m_logDirEdit = nullptr;
    QSpinBox  *m_maxSizeSpin = nullptr;
};
