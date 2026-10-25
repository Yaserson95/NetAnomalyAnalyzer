#pragma once

#include <QWidget>

class InterfaceManager;
class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;

/**
 * @brief Настройки прослушиваемых интерфейсов: добавление/удаление
 * (включение/выключение) устройств, BPF-фильтр, параметры захвата.
 */
class InterfacesSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit InterfacesSettingsWidget(InterfaceManager *manager, QWidget *parent = nullptr);

    void load();
    void apply();

private slots:
    void onRefreshClicked();

private:
    void rebuildDeviceList();

    InterfaceManager *m_manager;
    QListWidget      *m_list = nullptr;
    QLineEdit        *m_filterEdit = nullptr;
    QCheckBox        *m_promiscuousCheck = nullptr;
    QCheckBox        *m_autoStartCheck = nullptr;
    QSpinBox         *m_snaplenSpin = nullptr;
    QLabel           *m_hintLabel = nullptr;
    QPushButton      *m_refreshButton = nullptr;
};
