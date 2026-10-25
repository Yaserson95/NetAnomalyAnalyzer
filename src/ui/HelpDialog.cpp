#include "HelpDialog.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QProcess>
#include <QTextBrowser>
#include <QUrl>

namespace {

/**
 * Загрузить HTML-файл справки: из ресурсов, иначе из файла рядом с программой.
 * @param resource  путь в Qt-ресурсах (":/help/...");
 * @param beside    относительный путь к файлу рядом с exe (для разработки);
 * @param fallback  заглушка, если не найдено нигде.
 */
QString loadHelpHtml(const QString &resource, const QString &beside,
                     const QString &fallback)
{
    QFile res(resource);
    if (res.open(QIODevice::ReadOnly))
        return QString::fromUtf8(res.readAll());

    QFile dev(QApplication::applicationDirPath() + QLatin1Char('/') + beside);
    if (dev.open(QIODevice::ReadOnly))
        return QString::fromUtf8(dev.readAll());

    return fallback;
}

const QString kNotFound = QStringLiteral(
    "<html><body><p>Справка не найдена. Пересоберите её скриптом "
    "tools/build_help.py (исходник: help_src/manual.md).</p></body></html>");

} // namespace

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Справка — NetAnomalyAnalyzer"));
    resize(960, 680);

    // Левая колонка — неподвижное оглавление: отдельный документ, поэтому
    // при прокрутке тем остаётся на месте (при нехватке места скроллится
    // только по вертикали сама).
    m_toc = new QTextBrowser(this);
    m_toc->setOpenLinks(false);              // переход обрабатываем сами
    m_toc->setOpenExternalLinks(false);
    m_toc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_toc->setFixedWidth(250);
    m_toc->setStyleSheet(QStringLiteral(
        "QTextBrowser { background-color: #f2f5f8;"
        " border: none; border-right: 1px solid #d6dde4; }"));
    m_toc->setHtml(loadHelpHtml(QStringLiteral(":/help/manual_toc.html"),
                                QStringLiteral("help/manual_toc.html"),
                                kNotFound));

    // Правая колонка — темы справки; прокручивается только она.
    m_browser = new QTextBrowser(this);
    m_browser->setOpenLinks(true);           // переходы по внутренним ссылкам
    m_browser->setOpenExternalLinks(false);
    m_browser->setHtml(loadHelpHtml(QStringLiteral(":/help/manual_body.html"),
                                    QStringLiteral("help/manual_body.html"),
                                    kNotFound));

    // Клик по пункту оглавления — прокрутка текста к нужной теме.
    connect(m_toc, &QTextBrowser::anchorClicked, this,
            [this](const QUrl &url) {
                const QString anchor = url.fragment();
                m_browser->scrollToAnchor(anchor.isEmpty() ? url.toString()
                                                           : anchor);
            });

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_toc);
    layout->addWidget(m_browser, 1);
}

void HelpDialog::openSmart(QWidget *parent)
{
    // 1) Настоящий WinHelp: WinHlp32 (через реестр расширения .hlp) + файл.
    const QString hlp = QApplication::applicationDirPath()
        + QStringLiteral("/help/netanalyzer.hlp");
    const QString winhlp = qEnvironmentVariable("SystemRoot")
        + QStringLiteral("/help/winhlp32.exe");
    if (QFileInfo::exists(winhlp) && QFileInfo::exists(hlp)) {
        if (QProcess::startDetached(winhlp, {hlp}))
            return;
    }

    // 2) Встроенное окно справки (fallback без WinHlp32).
    static HelpDialog *dialog = nullptr;
    if (!dialog)
        dialog = new HelpDialog(parent);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
