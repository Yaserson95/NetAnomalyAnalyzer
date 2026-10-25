// Консольная и служебная оболочка ядра NetAnomalyAnalyzer.
//
// Ядро (захват, детект аномалий, журналы, статистика, оповещения, LLM)
// полностью отделено от GUI и используется здесь без графической
// оболочки — запуск на серверах, в консоли, либо как служба Windows.
//
// Режимы:
//   NetAnalyzerService               консольный запуск (Ctrl+C — остановка)
//   NetAnalyzerService --no-capture  без захвата (только журналы/LLM)
//   NetAnalyzerService --run-for N   завершиться через N секунд (проверка)
//   NetAnalyzerService --service     режим работы внутри SCM (служба)
//   NetAnalyzerService --install     установить службу (нужны права адм.)
//   NetAnalyzerService --uninstall   удалить службу
//   NetAnalyzerService --help        справка
//
// Настройки общие с GUI (ini-файл %ProgramData%\NetAnalyzer\NetAnomalyAnalyzer.ini)
// для обычного запуска; служба работает под своей учётной записью и
// пишет журналы рядом с exe (рабочий каталог службы меняется на каталог exe).
#include "anomaly/AnomalyDetector.h"
#include "app/AppController.h"
#include "core/InterfaceManager.h"
#include "llm/LlmScheduler.h"
#include "logging/LogWriter.h"
#include "notify/NotificationManager.h"
#include "settings/Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <string>

#ifdef Q_OS_WIN
#    include <windows.h>
#endif

namespace {

std::atomic<bool> g_stopRequested{false};
int    g_argc = 0;
char **g_argv = nullptr;

/** Перевод строк консольной оболочки. */
QString tr(const char *text)
{
    return QCoreApplication::translate("NetAnalyzerService", text);
}

void printLine(const QString &text)
{
    std::printf("[%s] %s\n",
                qPrintable(QDateTime::currentDateTime().toString(
                    QStringLiteral("HH:mm:ss"))),
                qPrintable(text));
    std::fflush(stdout);
}

void printUsage()
{
    std::printf(
        "NetAnalyzerService — ядро NetAnomalyAnalyzer без графической оболочки\n"
        "\n"
        "Консольный режим:\n"
        "  (без аргументов)   захват, журналы, оповещения, LLM-расписание\n"
        "  --no-capture       не запускать захват (журналы и LLM работают)\n"
        "  --run-for <сек>    завершиться через указанное число секунд\n"
        "  --help, -h         эта справка\n"
        "\n"
        "Служба Windows:\n"
        "  --install          установить службу NetAnalyzerService (автозапуск)\n"
        "  --uninstall        удалить службу\n"
        "  --service          режим работы внутри SCM (запускает служба)\n"
        "\n"
        "Управление службой:  sc start NetAnalyzerService / sc stop NetAnalyzerService\n"
        "Статус:              sc query NetAnalyzerService\n"
        "Журналы:             каталог logs (служба — рядом с executable)\n");
}

/** Значение числового аргумента вида "--имя 12" или "--имя=12". */
int argValue(const QStringList &args, const QString &name)
{
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == name && i + 1 < args.size())
            return args.at(i + 1).toInt();
        if (args.at(i).startsWith(name + QLatin1Char('=')))
            return args.at(i).mid(name.size() + 1).toInt();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Windows: обработчик консольных событий (Ctrl+C и закрытие окна)
// ---------------------------------------------------------------------------
#ifdef Q_OS_WIN
BOOL WINAPI consoleCtrlHandler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        g_stopRequested = true;
        return TRUE;                 // не завершать процесс сразу
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        // Дать главному циклу время на корректную остановку
        // (остановка захвата, запись в журнал), затем система завершит процесс.
        g_stopRequested = true;
        ::Sleep(4000);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

#ifndef Q_OS_WIN
void posixSignalHandler(int)
{
    g_stopRequested = true;
}
#endif

// ---------------------------------------------------------------------------
// Windows: служба SCM
// ---------------------------------------------------------------------------
#ifdef Q_OS_WIN
constexpr wchar_t kServiceName[] = L"NetAnalyzerService";

SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
SERVICE_STATUS        g_serviceStatus{};

void reportServiceState(DWORD state, DWORD exitCode = NO_ERROR,
                        DWORD waitHint = 0)
{
    g_serviceStatus.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwCurrentState            = state;
    g_serviceStatus.dwWin32ExitCode           = exitCode;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    g_serviceStatus.dwWaitHint                = waitHint;
    g_serviceStatus.dwControlsAccepted =
        (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING)
            ? 0
            : (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
    if (g_serviceStatusHandle)
        ::SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

void WINAPI serviceControlHandler(DWORD control)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        reportServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
        g_stopRequested = true;      // главный цикл остановится сам
    } else if (control == SERVICE_CONTROL_INTERROGATE) {
        if (g_serviceStatusHandle)
            ::SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
    }
}

int runCore(bool serviceMode, const QStringList &args);

void WINAPI serviceMain(DWORD /*argc*/, LPWSTR * /*argv*/)
{
    g_serviceStatusHandle = ::RegisterServiceCtrlHandlerW(
        kServiceName, &serviceControlHandler);
    if (!g_serviceStatusHandle)
        return;
    reportServiceState(SERVICE_START_PENDING, NO_ERROR, 10000);
    reportServiceState(SERVICE_RUNNING);
    runCore(true, QStringList());
    reportServiceState(SERVICE_STOPPED);
}

int runAsServiceDispatcher()
{
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), &serviceMain },
        { nullptr, nullptr }
    };
    if (!::StartServiceCtrlDispatcherW(table)) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // Запущен вручную с --service вне SCM — работаем как консоль.
            std::printf("--service запущен вне службы Windows; "
                        "продолжаю в консольном режиме.\n");
            return runCore(false, QStringList());
        }
        std::printf("StartServiceCtrlDispatcher: ошибка %lu\n", error);
        return 1;
    }
    return 0;
}

int installService()
{
    wchar_t modulePath[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring binPath = L"\"";
    binPath += modulePath;
    binPath += L"\" --service";

    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        std::printf("Не удалось открыть диспетчер служб (ошибка %lu). "
                    "Требуются права администратора.\n", ::GetLastError());
        return 1;
    }

    SC_HANDLE service = ::CreateServiceW(
        scm, kServiceName,
        L"NetAnomalyAnalyzer (анализ сетевых аномалий)",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL, binPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!service) {
        const DWORD error = ::GetLastError();
        ::CloseServiceHandle(scm);
        if (error == ERROR_SERVICE_EXISTS) {
            std::printf("Служба NetAnalyzerService уже установлена.\n");
            return 0;
        }
        std::printf("Не удалось установить службу (ошибка %lu). "
                    "Требуются права администратора.\n", error);
        return 1;
    }

    SERVICE_DESCRIPTIONW description;
    description.lpDescription = const_cast<LPWSTR>(
        L"Анализ сетевого трафика, журналы и оповещения об аномалиях "
        L"(NetAnomalyAnalyzer). Выполняется без графической оболочки.");
    ::ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);

    std::printf("Служба NetAnalyzerService установлена (автозапуск):\n  %ls\n",
                binPath.c_str());
    std::printf("Запуск:   sc start NetAnalyzerService  (net start NetAnalyzerService)\n"
                "Статус:   sc query NetAnalyzerService\n"
                "Удаление: NetAnalyzerService.exe --uninstall\n");
    ::CloseServiceHandle(service);
    ::CloseServiceHandle(scm);
    return 0;
}

int uninstallService()
{
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        std::printf("Не удалось открыть диспетчер служб (ошибка %lu). "
                    "Требуются права администратора.\n", ::GetLastError());
        return 1;
    }
    SC_HANDLE service = ::OpenServiceW(scm, kServiceName,
                                        SERVICE_STOP | DELETE);
    if (!service) {
        const DWORD error = ::GetLastError();
        ::CloseServiceHandle(scm);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::printf("Служба NetAnalyzerService не установлена.\n");
            return 0;
        }
        std::printf("Не удалось открыть службу (ошибка %lu). "
                    "Требуются права администратора.\n", error);
        return 1;
    }

    SERVICE_STATUS status;
    ::ControlService(service, SERVICE_CONTROL_STOP, &status);   // best effort
    if (!::DeleteService(service)) {
        const DWORD error = ::GetLastError();
        std::printf("Не удалось удалить службу (ошибка %lu).\n", error);
        ::CloseServiceHandle(service);
        ::CloseServiceHandle(scm);
        return 1;
    }
    std::printf("Служба NetAnalyzerService удалена.\n");
    ::CloseServiceHandle(service);
    ::CloseServiceHandle(scm);
    return 0;
}
#endif  // Q_OS_WIN

// ---------------------------------------------------------------------------
// Работа ядра (общая для консольного режима и службы)
// ---------------------------------------------------------------------------
int runCore(bool serviceMode, const QStringList &args)
{
    QCoreApplication app(g_argc, g_argv);
    // Имена организации/приложения обязаны совпадать с GUI-оболочкой:
    // настройки общие (ini-файл %ProgramData%\NetAnalyzer\NetAnomalyAnalyzer.ini).
    QCoreApplication::setOrganizationName(QStringLiteral("NetAnalyzer"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("netanalyzer.local"));
    QCoreApplication::setApplicationName(QStringLiteral("NetAnomalyAnalyzer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // Служба стартует с рабочим каталогом %SystemRoot%\System32 —
    // журналы должны падать рядом с executable, а не в System32.
    if (serviceMode)
        QDir::setCurrent(QCoreApplication::applicationDirPath());

    // Настройки должны существовать до создания контроллера.
    Settings::instance().reload();

    AppController controller;
    LogWriter *logWriter = controller.logWriter();

    // --- вывод событий в консоль (в службе stdout недоступен — безвредно) ---
    QObject::connect(controller.detector(), &AnomalyDetector::anomalyDetected,
                     &app, [](const Anomaly &anomaly) {
                         printLine(tr("Аномалия: %1")
                                       .arg(anomaly.notificationText()));
                     });
    QObject::connect(controller.llmScheduler(), &LlmScheduler::verdictAnomaly,
                     &app, [](const Anomaly &anomaly) {
                         printLine(tr("Аномалия (анализ LLM): %1")
                                       .arg(anomaly.notificationText()));
                     });
    QObject::connect(controller.notifications(),
                     &NotificationManager::notificationStatus, &app,
                     [](const QString &channel, const QString &status) {
                         printLine(tr("Канал [%1]: %2").arg(channel, status));
                     });
    QObject::connect(controller.llmScheduler(), &LlmScheduler::statusChanged,
                     &app, [](const QString &status) { printLine(status); });
    QObject::connect(controller.interfaces(), &InterfaceManager::captureError,
                     &app, [](const QString &device, const QString &message) {
                         printLine(tr("Ошибка захвата [%1]: %2")
                                       .arg(device, message));
                     });

    // --- сигналы остановки ---
#ifdef Q_OS_WIN
    ::SetConsoleCtrlHandler(&consoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, &posixSignalHandler);
    std::signal(SIGTERM, &posixSignalHandler);
#endif
    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &app, [] {
        if (g_stopRequested.load()) {
            printLine(tr("Получен сигнал остановки…"));
            QCoreApplication::quit();
        }
    });
    stopPoll.start(250);

    // --- запуск захвата (основная задача фонового режима) ---
    if (args.contains(QStringLiteral("--no-capture"))) {
        printLine(tr("Захват отключён (--no-capture), работают журналы и LLM"));
        logWriter->logError(QStringLiteral("service"),
                            tr("--no-capture: захват не запускался"));
    } else {
        const int started = controller.interfaces()->startAll();
        const QString message =
            started > 0
                ? tr("запуск: захват запущен на %1 интерфейсах").arg(started)
                : tr("запуск: захват не удалось запустить "
                     "(проверьте Npcap и права доступа)");
        printLine(message);
        logWriter->logError(QStringLiteral("service"), message);
    }

    // --- ограничение по времени (для проверок и планировщиков) ---
    const int runForSec = argValue(args, QStringLiteral("--run-for"));
    if (runForSec > 0) {
        QTimer::singleShot(runForSec * 1000, &app, [runForSec] {
            printLine(tr("--run-for %1 с: завершение по таймеру")
                          .arg(runForSec));
            QCoreApplication::quit();
        });
    }

    printLine(serviceMode
                  ? tr("Ядро запущено в режиме службы Windows")
                  : tr("Ядро запущено в консольном режиме (остановка — Ctrl+C)"));

    const int result = app.exec();

    // --- корректная остановка ---
    controller.interfaces()->stopAll();
    logWriter->logError(QStringLiteral("service"), tr("остановлен"));
    printLine(tr("Остановлено. Журналы: %1").arg(logWriter->logDirectory()));
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    g_argc = argc;
    g_argv = argv;

    QStringList args;
    for (int i = 1; i < argc; ++i)
        args.append(QString::fromLocal8Bit(argv[i]));

    if (args.contains(QStringLiteral("--help"))
        || args.contains(QStringLiteral("-h"))) {
        printUsage();
        return 0;
    }

#ifdef Q_OS_WIN
    if (args.contains(QStringLiteral("--install")))
        return installService();
    if (args.contains(QStringLiteral("--uninstall")))
        return uninstallService();
    if (args.contains(QStringLiteral("--service")))
        return runAsServiceDispatcher();
#else
    if (args.contains(QStringLiteral("--service"))
        || args.contains(QStringLiteral("--install"))
        || args.contains(QStringLiteral("--uninstall"))) {
        std::printf("Служба Windows доступна только в Windows-сборке.\n");
        return 1;
    }
#endif

    return runCore(false, args);
}
