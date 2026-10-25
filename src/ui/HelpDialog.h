#pragma once

#include <QDialog>

class QTextBrowser;

/**
 * @brief Встроенное окно справки (HTML из Qt-ресурсов).
 *
 * Используется, когда в системе нет просмотрщика WinHlp32 для открытия
 * help/netanalyzer.hlp. Содержимое генерируется скриптом tools/build_help.py
 * из help_src/manual.md и встраивается в ресурсы (resources/manual.qrc).
 *
 * Окно разделено на две независимые колонки: слева — неподвижное
 * оглавление (manual_toc.html), справа — темы (manual_body.html),
 * прокручивается только правая колонка. Клик по пункту оглавления
 * прокручивает текст к соответствующей теме.
 */
class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget *parent = nullptr);

    /**
     * @brief Открыть справку «умно».
     *
     * Если в системе установлен WinHlp32 и рядом с программой есть
     * help/netanalyzer.hlp — справка открывается во внешнем просмотрщике
     * (настоящий WinHelp с навигацией, поиском и закладками).
     * Иначе показывается встроенное окно справки с тем же содержанием.
     */
    static void openSmart(QWidget *parent);

private:
    QTextBrowser *m_toc = nullptr;     // оглавление (левая колонка, закреплено)
    QTextBrowser *m_browser = nullptr; // темы (правая колонка, скроллится)
};
