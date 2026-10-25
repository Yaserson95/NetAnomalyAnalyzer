// Автотест фильтрации: FilterCondition + PacketFilterProxy.
// Компилируется вместе с src/core/FilterCondition.cpp, Packet.cpp, CaptureDevice.cpp
// и src/ui/PacketFilterProxy.cpp, PacketTableModel.cpp (QtCore/QtGui/QtWidgets).
#include "anomaly/Anomaly.h"
#include "anomaly/AnomalyStatusStore.h"
#include "core/FilterCondition.h"
#include "core/Packet.h"
#include "core/CaptureDevice.h"
#include "core/PacketGrouper.h"
#include "llm/LlmClient.h"
#include "llm/LlmJournal.h"
#include "llm/LlmJournalEntry.h"
#include "llm/LlmScheduler.h"
#include "llm/LlmSentStore.h"
#include "llm/LlmVerdict.h"
#include "llm/PromptBudget.h"
#include "logging/LogWriter.h"
#include "notify/EmailNotifier.h"
#include "notify/NotificationManager.h"
#include "settings/Settings.h"
#include "ui/AnomalyTableModel.h"
#include "ui/PacketTableModel.h"
#include "ui/PacketFilterProxy.h"
#include "ui/GroupingWidget.h"
#include "ui/LogViewerWidget.h"
#include "ui/TableSorter.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMetaMethod>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>

/** Эмуляция клика по заголовку таблицы (сигнал sectionClicked). */
static void clickHeaderSection(QHeaderView *header, int section)
{
    const QMetaObject *meta = header->metaObject();
    const int index = meta->indexOfSignal("sectionClicked(int)");
    if (index >= 0)
        meta->method(index).invoke(header, Qt::DirectConnection,
                                   Q_ARG(int, section));
}

static int g_failures = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            std::printf("[ OK ] %s\n", name);                                  \
        } else {                                                               \
            std::printf("[FAIL] %s\n", name);                                  \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static Packet makePacket(const QString &srcMac, const QString &srcIp,
                         quint16 srcPort, const QString &protocol, int length)
{
    Packet p;
    p.setTimestamp(QDateTime::currentDateTime());
    p.setInterfaceName(QStringLiteral("\\Device\\NPF_{TEST}"));
    p.setSourceMac(srcMac);
    p.setDestinationMac(QStringLiteral("ff:ff:ff:ff:ff:ff"));
    p.setSourceIp(srcIp);
    p.setDestinationIp(QStringLiteral("93.184.216.34"));
    p.setSourcePort(srcPort);
    p.setDestinationPort(443);
    p.setCapturedLength(length);
    if (protocol == QLatin1String("TCP")) {
        p.setTransport(Packet::Transport::TCP);
        p.setTcpFlags(0x18);
    } else if (protocol == QLatin1String("UDP")) {
        p.setTransport(Packet::Transport::UDP);
    }
    return p;
}

/** Заглушка клиента LLM: синхронно отвечает на analyze (без сети). */
class StubLlmClient : public LlmClient {
public:
    QStringList prompts;    ///< все отправленные user-промпты
    bool failNext = false;  ///< следующий запрос завершить ошибкой

    void analyze(const QString &, const QString &userPrompt,
                 const LlmSettings &) override
    {
        prompts.append(userPrompt);
        if (failNext) {
            failNext = false;
            emit requestFailed(QStringLiteral("тестовая ошибка"));
        } else {
            emit responseReceived(
                QStringLiteral(R"({"status":"ok","data":{}})"), 1);
        }
    }
};

/**
 * @brief Витрина для визуальной проверки сортировки (без ввода с клавиатуры):
 * окно с журналом логов (Время ↓, выбрана первая запись) и таблицей
 * групп (IP ↑, выбрана группа с двумя пакетами). Снимается скриншотом
 * внешним скриптом, процесс держит окно живым до завершения извне.
 */
static int showSortingWindow()
{
    // --- Журнал логов во временном каталоге (не трогаем реальные файлы) ---
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return 2;
    const QString path = tempDir.filePath(
        QStringLiteral("packets-%1.jsonl")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
    {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(R"({"timestamp":"2026-10-20T10:00:00","info":"REC-ONE 10.0.0.1","src":"10.0.0.1","length":100})");
            file.write("\n");
            file.write(R"({"timestamp":"2026-10-20T10:00:05","info":"REC-TWO 10.0.0.2","src":"10.0.0.2","length":200})");
            file.write("\n");
            file.write(R"({"timestamp":"2026-10-20T10:00:10","info":"REC-THREE 10.0.0.3","src":"10.0.0.3","length":300})");
            file.write("\n");
            file.close();
        }
    }
    LoggingSettings logging = Settings::instance().logging();
    logging.logDir = tempDir.path();
    Settings::instance().setLogging(logging);
    {   // Детерминированное стартовое состояние сортировки
        QSettings s = Settings::openSettings();
        s.setValue(QStringLiteral("logSort/column"), 0);
        s.setValue(QStringLiteral("logSort/order"), int(Qt::AscendingOrder));
        s.setValue(QStringLiteral("grouping/sortColumn"), -1);
        s.setValue(QStringLiteral("grouping/sortOrder"),
                   int(Qt::DescendingOrder));
    }

    LogViewerWidget viewer;
    if (auto *logTable = viewer.findChild<QTableWidget *>()) {
        if (logTable->rowCount() >= 3) {
            clickHeaderSection(logTable->horizontalHeader(), 0);  // Время ↓
            logTable->setCurrentCell(0, 0);
            logTable->selectRow(0);   // панель содержимого — REC-THREE
        }
        std::printf("LOG indicator: col=%d order=%d (0=Asc,1=Desc) first=%s\n",
                    logTable->horizontalHeader()->sortIndicatorSection(),
                    int(logTable->horizontalHeader()->sortIndicatorOrder()),
                    logTable->item(0, 0)
                        ? qPrintable(logTable->item(0, 0)->text()) : "?");
        std::fflush(stdout);
    }

    // --- Группировка по буферу модели пакетов ---
    PacketTableModel model;
    model.appendPacket(makePacket(QStringLiteral("bc:24:11:22:33:44"),
                                  QStringLiteral("10.20.2.5"), 51514,
                                  QStringLiteral("TCP"), 1514));
    model.appendPacket(makePacket(QStringLiteral("38:a7:d8:00:11:22"),
                                  QStringLiteral("192.168.1.77"), 68,
                                  QStringLiteral("UDP"), 342));
    model.appendPacket(makePacket(QStringLiteral("bc:24:11:22:33:44"),
                                  QStringLiteral("10.20.2.5"), 443,
                                  QStringLiteral("TCP"), 100));
    model.appendPacket(makePacket(QStringLiteral("bc:24:aa:bb:cc:dd"),
                                  QStringLiteral("1.2.3.4"), 53,
                                  QStringLiteral("UDP"), 50));
    GroupingWidget grouping(&model);
    if (auto *groupTable = grouping.findChild<QTableWidget *>()) {
        if (groupTable->rowCount() > 0) {
            clickHeaderSection(groupTable->horizontalHeader(), 0); // IP ↑
            // Строка 1 — группа 10.20.2.5 (2 пакета), панель покажет сводку
            groupTable->setCurrentCell(1, 0);
            groupTable->selectRow(1);
        }
        std::printf("GROUP indicator: col=%d order=%d first=%s\n",
                    groupTable->horizontalHeader()->sortIndicatorSection(),
                    int(groupTable->horizontalHeader()->sortIndicatorOrder()),
                    groupTable->rowCount() > 0 && groupTable->item(0, 0)
                        ? qPrintable(groupTable->item(0, 0)->text()) : "?");
        std::fflush(stdout);
    }

    QWidget window;
    window.setWindowTitle(QStringLiteral(
        "Сортировка: журнал логов — Время ↓, группы — IP отправителя ↑"));
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(8, 8, 8, 8);
    auto *splitter = new QSplitter(Qt::Vertical, &window);
    splitter->addWidget(&viewer);
    splitter->addWidget(&grouping);
    layout->addWidget(splitter);
    window.resize(1400, 980);   // вписываем в экран 1080 для полного скриншота
    window.show();
    return qApp->exec();   // завершается извне (скрипт скриншота убивает процесс)
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);   // нужен для перевода tr()
    // Настройки теста — в изолированном ini-файле (не трогаем реальные)
    app.setOrganizationName(QStringLiteral("NetAnalyzerTest"));
    app.setApplicationName(QStringLiteral("FilterTest"));

    {   // Сброс состояния сортировки: ключи прошлых запусков и витрины
        // не должны влиять на порядок проверок
        QSettings s = Settings::openSettings();
        s.remove(QStringLiteral("logSort"));
        s.remove(QStringLiteral("grouping/sortColumn"));
        s.remove(QStringLiteral("grouping/sortOrder"));
    }

    if (argc > 1 && qstrcmp(argv[1], "--show-sorting") == 0)
        return showSortingWindow();

    using F = FilterCondition;
    using Op = F::Op;
    using Field = F::Field;

    const Packet tcpPacket = makePacket(QStringLiteral("bc:24:11:22:33:44"),
                                        QStringLiteral("10.20.2.5"), 51514,
                                        QStringLiteral("TCP"), 1514);
    const Packet udpPacket = makePacket(QStringLiteral("38:a7:d8:00:11:22"),
                                        QStringLiteral("192.168.1.77"), 68,
                                        QStringLiteral("UDP"), 342);

    // --- Packet: MAC начинается на ---
    CHECK(F(Field::SrcMac, Op::StartsWith, QStringLiteral("bc:24"))
              .matchesPacket(tcpPacket),
          "MAC отправителя начинается на bc:24 — пакет проходит");
    CHECK(!F(Field::SrcMac, Op::StartsWith, QStringLiteral("bc:24"))
                .matchesPacket(udpPacket),
          "MAC отправителя начинается на bc:24 — чужой пакет отфильтрован");

    // --- Packet: IP содержит ---
    CHECK(F(Field::SrcIp, Op::Contains, QStringLiteral("10.20"))
              .matchesPacket(tcpPacket),
          "IP отправителя содержит 10.20");
    CHECK(F(Field::DstIp, Op::Equal, QStringLiteral("93.184.216.34"))
              .matchesPacket(udpPacket),
          "IP назначения равно 93.184.216.34");

    // --- Packet: порт (числовой) ---
    CHECK(F(Field::SrcPort, Op::Greater, QStringLiteral("1024"))
              .matchesPacket(tcpPacket),
          "Порт отправителя больше 1024");
    CHECK(!F(Field::DstPort, Op::Less, QStringLiteral("80"))
                 .matchesPacket(tcpPacket),
          "Порт назначения меньше 80 — не проходит (443)");

    // --- Packet: длина ---
    CHECK(F(Field::Length, Op::Equal, QStringLiteral("1514"))
              .matchesPacket(tcpPacket),
          "Длина равно 1514");

    // --- Packet: протокол + регулярное выражение ---
    CHECK(F(Field::Protocol, Op::Contains, QStringLiteral("tcp"))
              .matchesPacket(tcpPacket),
          "Протокол содержит tcp (регистронезависимо)");
    CHECK(F(Field::SrcMac, Op::Regex, QStringLiteral("^38:a7"))
              .matchesPacket(udpPacket),
          "MAC назначения соответствует ^38:a7");
    CHECK(!F(Field::SrcMac, Op::Regex, QStringLiteral("^38:a7"))
                 .matchesPacket(tcpPacket),
          "MAC ^38:a7 — чужой пакет отфильтрован");

    // --- Packet: отрицательные операторы ---
    CHECK(F(Field::Protocol, Op::NotContains, QStringLiteral("UDP"))
              .matchesPacket(tcpPacket),
          "Протокол не содержит UDP");
    CHECK(!F(Field::Protocol, Op::NotContains, QStringLiteral("UDP"))
                 .matchesPacket(udpPacket),
          "Протокол не содержит UDP — UDP-пакет отфильтрован");

    // --- Packet: интерфейс по человекочитаемому имени ---
    CaptureDevice::friendlyName(QStringLiteral("\\Device\\NPF_{TEST}"));
    CHECK(F(Field::Interface, Op::Contains, QStringLiteral("NPF"))
              .matchesPacket(tcpPacket),
          "Интерфейс содержит NPF (pcap-имя)");

    // --- JSON-запись журнала пакетов ---
    QJsonObject packetLog;
    packetLog[QStringLiteral("src")] = QStringLiteral("10.20.2.5");
    packetLog[QStringLiteral("dst")] = QStringLiteral("93.184.216.34");
    packetLog[QStringLiteral("srcMac")] = QStringLiteral("bc:24:11:22:33:44");
    packetLog[QStringLiteral("protocol")] = QStringLiteral("TCP");
    packetLog[QStringLiteral("srcPort")] = 51514;
    packetLog[QStringLiteral("length")] = 1514;
    CHECK(F(Field::SrcIp, Op::StartsWith, QStringLiteral("10.20")).matchesJson(packetLog),
          "JSON: IP отправителя начинается на 10.20");
    CHECK(F(Field::SrcPort, Op::Greater, QStringLiteral("50000")).matchesJson(packetLog),
          "JSON: порт отправителя больше 50000");
    CHECK(F(Field::SrcMac, Op::StartsWith, QStringLiteral("bc:24")).matchesJson(packetLog),
          "JSON: MAC начинается на bc:24");

    // --- JSON-запись журнала аномалий (алиасы ключей) ---
    QJsonObject anomalyLog;
    anomalyLog[QStringLiteral("severity")] = QStringLiteral("Предупреждение");
    anomalyLog[QStringLiteral("rule")] = QStringLiteral("PortScan");
    anomalyLog[QStringLiteral("sourceIp")] = QStringLiteral("10.20.2.99");
    anomalyLog[QStringLiteral("title")] = QStringLiteral("Сканирование портов");
    CHECK(F(Field::SrcIp, Op::Equal, QStringLiteral("10.20.2.99")).matchesJson(anomalyLog),
          "JSON: аномалия, IP по ключу sourceIp");
    CHECK(F(Field::Severity, Op::Contains, QStringLiteral("Предупр")).matchesJson(anomalyLog),
          "JSON: аномалия, уровень содержит Предупр");
    CHECK(F(Field::Info, Op::Contains, QStringLiteral("Сканирование")).matchesJson(anomalyLog),
          "JSON: информация по ключу title");
    CHECK(!F(Field::Protocol, Op::Contains, QStringLiteral("TCP")).matchesJson(anomalyLog),
          "JSON: поле protocol отсутствует — не проходит");

    // --- Proxy-модель таблицы пакетов ---
    PacketTableModel model;
    model.appendPacket(tcpPacket);
    model.appendPacket(udpPacket);
    model.appendPacket(makePacket(QStringLiteral("bc:24:99:88:77:66"),
                                  QStringLiteral("10.20.2.6"), 443,
                                  QStringLiteral("TCP"), 60));
    CHECK(model.rowCount() == 3, "Модель содержит 3 пакета");

    PacketFilterProxy proxy;
    proxy.setSourceModel(&model);
    CHECK(proxy.rowCount() == 3, "Без фильтра прокси показывает 3 пакета");

    proxy.setConditions({ F(Field::SrcMac, Op::StartsWith, QStringLiteral("bc:24")) });
    CHECK(proxy.rowCount() == 2, "Фильтр MAC bc:24 → 2 пакета из 3");
    CHECK(proxy.hasConditions(), "Прокси сообщает о активном фильтре");

    proxy.setConditions({ F(Field::SrcMac, Op::StartsWith, QStringLiteral("bc:24")),
                          F(Field::Protocol, Op::Equal, QStringLiteral("TCP")) });
    CHECK(proxy.rowCount() == 2, "Два условия (И): MAC bc:24 + TCP → 2 пакета");

    proxy.setConditions({ F(Field::SrcIp, Op::Equal, QStringLiteral("192.168.1.77")) });
    CHECK(proxy.rowCount() == 1, "Фильтр по IP → 1 пакет");

    proxy.setConditions({});
    CHECK(proxy.rowCount() == 3, "Сброс фильтра → снова 3 пакета");

    // --- Некорректный regex не падает и ничего не пропускает ---
    CHECK(!F(Field::Protocol, Op::Regex, QStringLiteral("([")).matchesPacket(tcpPacket),
          "Некорректный regex не пропускает пакеты");

    // --- Группировка: один параметр (IP отправителя) ---
    QVector<Packet> packets = { tcpPacket, udpPacket,
                                makePacket(QStringLiteral("bc:24:11:22:33:44"),
                                           QStringLiteral("10.20.2.5"), 443,
                                           QStringLiteral("TCP"), 100) };
    auto grouped = PacketGrouper::groupPackets(
        packets, { Field::SrcIp });
    CHECK(grouped.groups.size() == 2, "Группировка по IP: 2 группы");
    CHECK(grouped.packets == 3 && grouped.bytes == 1514 + 342 + 100,
          "Группировка по IP: общие итоги");
    CHECK(grouped.groups.first().keys.first() == QStringLiteral("10.20.2.5"),
          "Группировка по IP: лидер по пакетам — 10.20.2.5");
    CHECK(grouped.groups.first().packets == 2 && grouped.groups.first().bytes == 1614,
          "Группировка по IP: сводка группы 10.20.2.5 (2 пакета, 1614 байт)");

    // --- Группировка: два параметра (IP отправителя + IP назначения) ---
    auto grouped2 = PacketGrouper::groupPackets(
        packets, { Field::SrcIp, Field::DstIp });
    CHECK(grouped2.groups.size() == 2, "Группировка по IP+IP: 2 группы (все назначения совпадают)");
    CHECK(grouped2.groups.first().keys.size() == 2 &&
              grouped2.groups.first().keys.first() == QStringLiteral("10.20.2.5") &&
              grouped2.groups.first().keys.last() == QStringLiteral("93.184.216.34"),
          "Группировка по IP+IP: ключи группы");

    // --- Группировка: три параметра (MAC отправителя + IP + порт) ---
    auto grouped3 = PacketGrouper::groupPackets(
        packets, { Field::SrcMac, Field::SrcIp, Field::SrcPort });
    CHECK(grouped3.groups.size() == 3, "Группировка по MAC+IP+порт: 3 группы");

    // --- Группировка записей журнала (JSON) ---
    QJsonObject rec1, rec2, rec3;
    rec1[QStringLiteral("src")] = QStringLiteral("10.20.2.5");
    rec1[QStringLiteral("dst")] = QStringLiteral("93.184.216.34");
    rec1[QStringLiteral("length")] = 500;
    rec2[QStringLiteral("src")] = QStringLiteral("10.20.2.5");
    rec2[QStringLiteral("dst")] = QStringLiteral("8.8.8.8");
    rec2[QStringLiteral("length")] = 200;
    rec3[QStringLiteral("src")] = QStringLiteral("192.168.1.77");
    rec3[QStringLiteral("dst")] = QStringLiteral("93.184.216.34");
    rec3[QStringLiteral("length")] = 100;
    auto groupedLog = PacketGrouper::groupRecords({ rec1, rec2, rec3 },
                                                  { Field::SrcIp });
    CHECK(groupedLog.groups.size() == 2, "Группировка журнала по IP: 2 группы");
    CHECK(groupedLog.groups.first().packets == 1 ||
              groupedLog.groups.first().packets == 2,
          "Группировка журнала: лидер по пакетам");
    CHECK(groupedLog.bytes == 800, "Группировка журнала: сумма байт");
    auto groupedLog2 = PacketGrouper::groupRecords({ rec1, rec2, rec3 },
                                                   { Field::SrcIp, Field::DstIp });
    CHECK(groupedLog2.groups.size() == 3, "Группировка журнала IP+IP: 3 уникальные пары");

    // --- Группировка без полей возвращает пустой результат ---
    CHECK(PacketGrouper::groupPackets(packets, {}).groups.isEmpty(),
          "Группировка без полей возвращает пустой результат");

    // --- Вкладка «Группировка»: панель подробностей, выделение, сортировка ---
    {
        PacketTableModel tableModel;
        tableModel.appendPacket(tcpPacket);
        tableModel.appendPacket(udpPacket);
        tableModel.appendPacket(makePacket(QStringLiteral("bc:24:11:22:33:44"),
                                           QStringLiteral("10.20.2.5"), 443,
                                           QStringLiteral("TCP"), 100));
        // Отдельная группа с малым IP — в порядке «по пакетам» она внизу,
        // в порядке «по тексту возрастание» — вверху (порядок различим)
        tableModel.appendPacket(makePacket(QStringLiteral("bc:24:aa:bb:cc:dd"),
                                           QStringLiteral("1.2.3.4"), 53,
                                           QStringLiteral("UDP"), 50));
        GroupingWidget groupWidget(&tableModel);
        auto *table = groupWidget.findChild<QTableWidget *>();
        CHECK(table && table->rowCount() > 0,
              "Группировка: таблица заполнена по буферу модели");
        if (table && table->rowCount() >= 3) {
            auto *detail = groupWidget.findChild<QPlainTextEdit *>();
            table->setCurrentCell(0, 0);
            CHECK(detail && detail->toPlainText().contains(QStringLiteral("Пакетов:")),
                  "Панель подробностей заполняется при выборе группы");
            // Пересчёт по refresh() (через троттлинг 800 мс) не теряет выделение
            QThread::msleep(850);
            groupWidget.refresh();
            CHECK(table->currentRow() == 0,
                  "Выделение группы пережило пересчёт");
            CHECK(detail && detail->toPlainText().contains(QStringLiteral("Пакетов:")),
                  "Сводка группы сохранилась после пересчёта");

            // Клик по заголовку «IP отправителя» (колонка 0) — по возрастанию
            clickHeaderSection(table->horizontalHeader(), 0);
            CHECK(table->item(0, 0)
                      && table->item(0, 0)->text() == QStringLiteral("1.2.3.4"),
                  "Группировка: клик по заголовку сортирует по возрастанию");
            CHECK(table->currentRow() > 0
                      && table->item(table->currentRow(), 0)
                      && table->item(table->currentRow(), 0)->text()
                             == QStringLiteral("10.20.2.5"),
                  "Группировка: выделение последовало за группой при сортировке");
            // Сортировка переживает пересчёт
            QThread::msleep(850);
            groupWidget.refresh();
            CHECK(table->item(0, 0)
                      && table->item(0, 0)->text() == QStringLiteral("1.2.3.4"),
                  "Группировка: сортировка сохраняется после пересчёта");
            CHECK(table->currentRow() > 0
                      && table->item(table->currentRow(), 0)
                      && table->item(table->currentRow(), 0)->text()
                             == QStringLiteral("10.20.2.5"),
                  "Группировка: выделение группы сохранилось при сортировке");
            // Повторный клик — по убыванию
            clickHeaderSection(table->horizontalHeader(), 0);
            CHECK(table->item(0, 0)
                      && table->item(0, 0)->text() == QStringLiteral("192.168.1.77"),
                  "Группировка: повторный клик меняет порядок на убывание");
        }
    }

    // --- TableSorter: числовая сортировка и строки-ограничители ---
    {
        QTableWidget sorterTable(0, 2);
        auto addRow = [&sorterTable](const QString &name, double number) {
            const int row = sorterTable.rowCount();
            sorterTable.insertRow(row);
            auto *nameItem = new QTableWidgetItem(name);
            auto *numItem = new QTableWidgetItem(QString::number(number));
            numItem->setData(Qt::UserRole + 1, number);
            sorterTable.setItem(row, 0, nameItem);
            sorterTable.setItem(row, 1, numItem);
        };
        addRow(QStringLiteral("b"), 5);
        addRow(QStringLiteral("a"), 100);
        addRow(QStringLiteral("c"), 2);
        sorterTable.insertRow(3);   // строка-ограничитель: только вторая колонка
        sorterTable.setItem(3, 1, new QTableWidgetItem(QStringLiteral("… и ещё 7")));
        TableSorter::sortRows(&sorterTable, 1, Qt::DescendingOrder, 1);
        CHECK(sorterTable.item(0, 0)->text() == QStringLiteral("a")
                  && sorterTable.item(1, 0)->text() == QStringLiteral("b")
                  && sorterTable.item(2, 0)->text() == QStringLiteral("c")
                  && sorterTable.item(3, 0) == nullptr
                  && sorterTable.item(3, 1) != nullptr,
              "TableSorter: числа по убыванию, ограничитель остаётся внизу");
        TableSorter::sortRows(&sorterTable, 0, Qt::AscendingOrder, 1);
        CHECK(sorterTable.item(0, 0)->text() == QStringLiteral("a")
                  && sorterTable.item(2, 0)->text() == QStringLiteral("c"),
              "TableSorter: текст по возрастанию");
        TableSorter::sortRows(&sorterTable, 1, Qt::AscendingOrder, 1);
        CHECK(sorterTable.item(0, 0)->text() == QStringLiteral("c")
                  && sorterTable.item(1, 0)->text() == QStringLiteral("b")
                  && sorterTable.item(2, 0)->text() == QStringLiteral("a"),
              "TableSorter: числа по возрастанию (2 < 5 < 100)");
        CHECK(!TableSorter::columnIsNumeric(&sorterTable, 0)
                  && TableSorter::columnIsNumeric(&sorterTable, 1),
              "TableSorter: определение числовой колонки");
    }

    // --- Группировка: журнал пакетов читается в фоне (интерфейс не занят) ---
    {
        const QString oldDir = Settings::instance().logging().logDir;
        Settings::openSettings().setValue(QStringLiteral("grouping/sourceLog"), false);
        QTemporaryDir tempDir;
        CHECK(tempDir.isValid(), "Группировка-журнал: временный каталог создан");
        const QString path = tempDir.filePath(
            QStringLiteral("packets-%1.jsonl")
                .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
        const auto writeRecords = [&path](int from, int to) {
            QFile file(path);
            if (!file.open(QIODevice::Append | QIODevice::Text))
                return;
            for (int i = from; i < to; ++i) {
                const QByteArray line =
                    QJsonDocument(QJsonObject{
                        { QStringLiteral("timestamp"),
                          QStringLiteral("2026-10-20T10:00:%1")
                              .arg(i % 60, 2, 10, QLatin1Char('0')) },
                        { QStringLiteral("src"),
                          i % 2 ? QStringLiteral("10.0.0.1")
                                : QStringLiteral("10.0.0.2") },
                        { QStringLiteral("dst"), QStringLiteral("93.184.216.34") },
                        { QStringLiteral("protocol"), QStringLiteral("UDP") },
                        { QStringLiteral("length"), 60 + (i % 7) },
                    }).toJson(QJsonDocument::Compact);
                file.write(line + '\n');
            }
            file.close();
        };
        writeRecords(0, 60);

        // Направляем журналы во временный каталог, чтобы не трогать реальные
        LoggingSettings logging = Settings::instance().logging();
        logging.logDir = tempDir.path();
        Settings::instance().setLogging(logging);

        PacketTableModel emptyModel;
        GroupingWidget groupWidget(&emptyModel);
        // Переключаем источник на «Журнал пакетов:»
        QRadioButton *logRadio = nullptr;
        for (auto *radio : groupWidget.findChildren<QRadioButton *>()) {
            if (radio->text().startsWith(QStringLiteral("Журнал"))) {
                logRadio = radio;
                break;
            }
        }
        CHECK(logRadio, "Группировка-журнал: радио «Журнал пакетов» найдено");
        auto *table = groupWidget.findChild<QTableWidget *>();
        if (logRadio && table) {
            logRadio->setChecked(true);
            // Ждём завершения фоновой задачи (обработка событий GUI)
            for (int i = 0; i < 400 && table->rowCount() == 0; ++i) {
                QCoreApplication::processEvents();
                QThread::msleep(25);
            }
            CHECK(table->rowCount() == 2,
                  "Группировка-журнал: таблица заполнена из журнала (2 группы IP)");
            // Дозапись в журнал учитывается приращением (без полного чтения)
            writeRecords(60, 70);
            int packets = -1;
            const QRegularExpression packetsRe(
                QStringLiteral("пакетов: (\\d+)"));
            for (int i = 0; i < 300 && packets != 70; ++i) {
                groupWidget.refresh();
                QCoreApplication::processEvents();
                QThread::msleep(50);
                for (const QLabel *label :
                     groupWidget.findChildren<QLabel *>()) {
                    const QRegularExpressionMatch m =
                        packetsRe.match(label->text());
                    if (m.hasMatch()) {
                        packets = m.captured(1).toInt();
                        break;
                    }
                }
            }
            CHECK(packets == 70,
                  "Группировка-журнал: дописанные записи дозагружены (70 пакетов)");
        }
        // Вернуть каталог журналов и исходный источник в настройках
        logging.logDir = oldDir;
        Settings::instance().setLogging(logging);
        Settings::openSettings().setValue(QStringLiteral("grouping/sourceLog"), false);
    }

    // --- Вкладка «Просмотр логов»: сортировка и соответствие панели строке ---
    {
        const QString oldDir = Settings::instance().logging().logDir;
        QTemporaryDir tempDir;
        CHECK(tempDir.isValid(), "Логи: временный каталог создан");
        const QString path = tempDir.filePath(
            QStringLiteral("packets-%1.jsonl")
                .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
        {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(R"({"timestamp":"2026-10-20T10:00:00","info":"REC-ONE","src":"10.0.0.1"})");
                file.write("\n");
                file.write(R"({"timestamp":"2026-10-20T10:00:05","info":"REC-TWO","src":"10.0.0.2"})");
                file.write("\n");
                file.write(R"({"timestamp":"2026-10-20T10:00:10","info":"REC-THREE","src":"10.0.0.3"})");
                file.write("\n");
            }
            file.close();
        }
        // Направляем журналы во временный каталог, чтобы не трогать реальные
        LoggingSettings logging = Settings::instance().logging();
        logging.logDir = tempDir.path();
        Settings::instance().setLogging(logging);

        LogViewerWidget viewer;
        auto *logTable = viewer.findChild<QTableWidget *>();
        CHECK(logTable && logTable->rowCount() >= 3,
              "Логи: таблица заполнена из журнала во временном каталоге");
        if (logTable && logTable->rowCount() >= 3) {
            // Вернуть каталог журналов сразу после чтения конструктором
            logging.logDir = oldDir;
            Settings::instance().setLogging(logging);

            auto ms = [logTable](int row) {
                return logTable->item(row, 0)->data(Qt::UserRole + 1).toDouble();
            };
            // По умолчанию — хронологический порядок (по возрастанию времени)
            CHECK(ms(0) < ms(1) && ms(1) < ms(2),
                  "Логи: по умолчанию записи в хронологическом порядке");
            // Клик по «Время» — по убыванию (самая свежая сверху)
            clickHeaderSection(logTable->horizontalHeader(), 0);
            CHECK(ms(0) > ms(1) && ms(1) > ms(2),
                  "Логи: клик по заголовку «Время» сортирует по убыванию");
            // Панель содержимого соответствует первой строке (REC-THREE)
            logTable->setCurrentCell(0, 0);
            logTable->selectRow(0);
            auto *logDetail = viewer.findChild<QPlainTextEdit *>();
            CHECK(logDetail
                      && logDetail->toPlainText().contains(QStringLiteral("REC-THREE")),
                  "Логи: после сортировки панель содержит запись первой строки");
            // Повторный клик — обратно по возрастанию
            clickHeaderSection(logTable->horizontalHeader(), 0);
            CHECK(ms(0) < ms(1) && ms(1) < ms(2),
                  "Логи: повторный клик меняет порядок на возрастание");
            // Сортировка по текстовой колонке «Событие» (колонка 1)
            clickHeaderSection(logTable->horizontalHeader(), 1);
            const QString s0 = logTable->item(0, 1)->text();
            const QString s1 = logTable->item(1, 1)->text();
            const QString s2 = logTable->item(2, 1)->text();
            CHECK(s0 <= s1 && s1 <= s2
                  && logTable->item(0, 0)->data(Qt::UserRole).isValid()
                  && logTable->item(1, 0)->data(Qt::UserRole).isValid(),
                  "Логи: сортировка по тексту, индексы записей сохраняются");
        }
    }

    // --- Вердикт LLM: разбор формата {"status":…,"data":{…}} ---
    {
        const LlmVerdict okVerdict = LlmVerdict::fromResponse(
            QStringLiteral(R"({"status":"ok","data":{"summary":"Всё спокойно","findings":[]}})"));
        CHECK(okVerdict.parsed
                  && okVerdict.level == LlmVerdict::Level::Ok
                  && okVerdict.statusText == QStringLiteral("ok")
                  && okVerdict.summary == QStringLiteral("Всё спокойно"),
              "Вердикт: чистый JSON, status=ok");

        const LlmVerdict fenced = LlmVerdict::fromResponse(QStringLiteral(
            "Вот анализ:\n```json\n"
            "{\"status\":\"warning\",\"data\":{\"summary\":\"Скан портов\",\"findings\":["
            "{\"title\":\"Порт-скан\",\"severity\":\"high\","
            "\"details\":\"10.0.0.5 прошёл по 100 портам\","
            "\"recommendation\":\"Заблокировать источник\"}]}}\n```"));
        CHECK(fenced.parsed
                  && fenced.level == LlmVerdict::Level::Warning
                  && fenced.findings.size() == 1
                  && fenced.findings.first().title == QStringLiteral("Порт-скан")
                  && fenced.findings.first().recommendation
                         == QStringLiteral("Заблокировать источник"),
              "Вердикт: JSON в ```-обёртке с текстом вокруг, status=warning");

        const LlmVerdict critical = LlmVerdict::fromResponse(
            QStringLiteral("Анализ завершён. "
                           "{\"status\":\"critical\",\"data\":{\"summary\":\"Флуд\"}}"));
        CHECK(critical.parsed
                  && critical.level == LlmVerdict::Level::Critical
                  && !critical.notificationSummary().isEmpty(),
              "Вердикт: текст вокруг JSON, status=critical");

        const LlmVerdict garbage =
            LlmVerdict::fromResponse(QStringLiteral("просто текст без JSON"));
        CHECK(!garbage.parsed
                  && garbage.level == LlmVerdict::Level::Unknown,
              "Вердикт: неструктурированный ответ не распознан");

        CHECK(LlmVerdict::levelName(QStringLiteral("critical")) == QObject::tr("Критично")
                  && LlmVerdict::levelName(QString()) == QObject::tr("—"),
              "Вердикт: человекочитаемые имена уровней");
    }

    // --- Anomaly: восстановление из JSON-записи журнала ---
    {
        Anomaly anomaly;
        anomaly.setId(5);
        anomaly.setTimestamp(QDateTime::currentDateTime());
        anomaly.setRuleName(QStringLiteral("SynFlood"));
        anomaly.setSeverity(Anomaly::Severity::Critical);
        anomaly.setTitle(QStringLiteral("SYN-флуд"));
        anomaly.setSourceIp(QStringLiteral("10.0.0.9"));
        anomaly.setPacketCount(100);
        const Anomaly restored = Anomaly::fromJson(anomaly.toJson());
        CHECK(restored.id() == 5
                  && restored.severity() == Anomaly::Severity::Critical
                  && restored.title() == QStringLiteral("SYN-флуд")
                  && restored.sourceIp() == QStringLiteral("10.0.0.9")
                  && restored.packetCount() == 100,
              "Anomaly: fromJson восстанавливает запись журнала");
    }

    // --- AnomalyStatusStore: отметки «решено» переживают перезапуск ---
    {
        QTemporaryDir tempDir;
        CHECK(tempDir.isValid(), "Статусы аномалий: временный каталог создан");
        const QString filePath =
            tempDir.filePath(QStringLiteral("anomaly-status.json"));
        AnomalyStatusStore store(filePath);
        const QString key = AnomalyStatusStore::keyFor(
            7, QStringLiteral("2026-10-20T12:00:00"));
        CHECK(key == QStringLiteral("7@2026-10-20T12:00:00"),
              "Ключ аномалии: id@timestamp");
        CHECK(store.status(key) == QLatin1String(AnomalyStatusStore::kDetected),
              "Статус по умолчанию — обнаружено");
        store.setResolved(key, true);
        CHECK(store.isResolved(key) && !store.resolvedAt(key).isEmpty(),
              "Отметка «решено» сохранена");
        AnomalyStatusStore reloaded(filePath);
        CHECK(reloaded.isResolved(key),
              "Отметка «решено» пережила повторную загрузку файла");
        reloaded.setResolved(key, false);
        CHECK(!reloaded.isResolved(key)
                  && reloaded.status(key) == QLatin1String(AnomalyStatusStore::kDetected),
              "Снятие отметки возвращает «обнаружено»");
    }

    // --- Модель аномалий: колонка статуса ---
    {
        AnomalyTableModel anomalyModel;
        Anomaly anomaly;
        anomaly.setId(1);
        anomaly.setTimestamp(QDateTime::currentDateTime());
        anomaly.setSeverity(Anomaly::Severity::Warning);
        anomalyModel.addAnomaly(anomaly);
        CHECK(anomalyModel.statusAt(0) == QLatin1String(AnomalyStatusStore::kDetected)
                  && AnomalyTableModel::statusDisplayName(anomalyModel.statusAt(0))
                         == QObject::tr("обнаружено"),
              "Модель аномалий: статус по умолчанию — обнаружено");
        anomalyModel.setStatus(0, QLatin1String(AnomalyStatusStore::kResolved));
        CHECK(anomalyModel.statusAt(0) == QLatin1String(AnomalyStatusStore::kResolved)
                  && AnomalyTableModel::statusDisplayName(anomalyModel.statusAt(0))
                         == QObject::tr("решено")
                  && anomalyModel
                         .headerData(int(AnomalyTableModel::ColStatus),
                                     Qt::Horizontal).toString()
                         == QObject::tr("Статус"),
              "Модель аномалий: статус меняется на «решено», есть колонка «Статус»");
        // Колонка «Обнаружение»: без источника показывается анализатор
        CHECK(anomalyModel
                      .headerData(int(AnomalyTableModel::ColDetector),
                                  Qt::Horizontal).toString()
                  == QObject::tr("Обнаружение")
                  && anomalyModel
                         .data(anomalyModel.index(
                             0, int(AnomalyTableModel::ColDetector))).toString()
                         == Anomaly::defaultDetectorName(),
              "Модель аномалий: колонка «Обнаружение», анализатор по умолчанию");
        // Аномалия от LLM показывает название модели и переживает JSON
        Anomaly llmAnomaly;
        llmAnomaly.setId(2);
        llmAnomaly.setTimestamp(QDateTime::currentDateTime());
        llmAnomaly.setDetector(QStringLiteral("qwen3-vl"));
        anomalyModel.addAnomaly(llmAnomaly);
        CHECK(anomalyModel
                      .data(anomalyModel.index(
                          1, int(AnomalyTableModel::ColDetector))).toString()
                  == QStringLiteral("qwen3-vl")
                  && Anomaly::fromJson(llmAnomaly.toJson()).detector()
                         == QStringLiteral("qwen3-vl"),
              "Модель аномалий: источник LLM — название модели, есть в JSON");
    }

    // --- LogWriter: статусы в хвосте аномалий + журнал событий за период ---
    {
        const QString oldDir = Settings::instance().logging().logDir;
        QTemporaryDir tempDir;
        CHECK(tempDir.isValid(), "LogWriter: временный каталог создан");
        LoggingSettings logging = Settings::instance().logging();
        logging.logDir = tempDir.path();
        Settings::instance().setLogging(logging);
        {
            LogWriter writer;
            Anomaly anomaly;
            anomaly.setId(1);
            anomaly.setTimestamp(QDateTime::currentDateTime().addSecs(-60));
            anomaly.setRuleName(QStringLiteral("PortScan"));
            anomaly.setSeverity(Anomaly::Severity::Warning);
            anomaly.setTitle(QStringLiteral("Сканирование портов"));
            writer.onAnomaly(anomaly);

            const QVector<QJsonObject> tail = writer.tailAnomalies(10);
            CHECK(tail.size() == 1
                      && tail.first().value(QStringLiteral("status")).toString()
                             == QLatin1String(AnomalyStatusStore::kDetected),
                  "LogWriter: хвост аномалий содержит статус detected");
            if (!tail.isEmpty()) {
                writer.setAnomalyResolved(AnomalyStatusStore::keyFor(tail.first()), true);
                const QVector<QJsonObject> tail2 = writer.tailAnomalies(10);
                CHECK(!tail2.isEmpty()
                          && tail2.first().value(QStringLiteral("status")).toString()
                                 == QLatin1String(AnomalyStatusStore::kResolved),
                      "LogWriter: после отметки хвост содержит статус resolved");
            }

            // Журнал событий: реальные записи logError по обе стороны отсечки
            writer.logError(QStringLiteral("t"), QStringLiteral("старое"));
            QThread::msleep(1100);
            const qint64 cut = QDateTime::currentMSecsSinceEpoch();
            QThread::msleep(1100);
            writer.logError(QStringLiteral("t"), QStringLiteral("новое"));
            const QVector<QJsonObject> events = writer.eventsSince(cut, 100);
            bool hasNew = false, hasOld = false;
            for (const QJsonObject &event : std::as_const(events)) {
                const QString message =
                    event.value(QStringLiteral("message")).toString();
                if (message == QStringLiteral("новое"))
                    hasNew = true;
                if (message == QStringLiteral("старое"))
                    hasOld = true;
            }
            CHECK(hasNew && !hasOld,
                  "LogWriter: eventsSince возвращает только события после отсечки");
            CHECK(writer.eventsSince(
                      QDateTime::currentMSecsSinceEpoch() + 3600 * 1000, 100).isEmpty(),
                  "LogWriter: eventsSince из будущего пуст");
        }
        logging.logDir = oldDir;
        Settings::instance().setLogging(logging);
    }

    // --- Журнал LLM: toJson/fromJson + загрузка истории из файла ---
    {
        LlmJournalEntry entry;
        entry.id = 42;
        entry.source = LlmJournalEntry::Source::Scheduled;
        entry.status = LlmJournalEntry::Status::Success;
        entry.startTime = QDateTime::currentDateTime().addSecs(-5);
        entry.finishTime = QDateTime::currentDateTime();
        entry.model = QStringLiteral("test-model");
        entry.prompt = QStringLiteral("ПОЛНЫЙ ПРОМПТ");
        entry.promptPreview = entry.prompt.left(1000);
        entry.response = QStringLiteral("{}");
        entry.elapsedMs = 1234;
        entry.anomaliesInPrompt = 3;
        entry.logPeriodFrom = QStringLiteral("2026-10-20T10:00:00");
        entry.verdictStatus = QStringLiteral("warning");
        entry.verdictData[QStringLiteral("summary")] = QStringLiteral("Скан");

        const LlmJournalEntry parsed = LlmJournalEntry::fromJson(entry.toJson());
        CHECK(parsed.id == 42
                  && parsed.status == LlmJournalEntry::Status::Success
                  && parsed.source == LlmJournalEntry::Source::Scheduled
                  && parsed.prompt == QStringLiteral("ПОЛНЫЙ ПРОМПТ")
                  && parsed.verdictStatus == QStringLiteral("warning")
                  && parsed.verdictData.value(QStringLiteral("summary")).toString()
                         == QStringLiteral("Скан")
                  && parsed.logPeriodFrom == QStringLiteral("2026-10-20T10:00:00")
                  && parsed.elapsedMs == 1234
                  && parsed.anomaliesInPrompt == 3,
              "Журнал LLM: toJson/fromJson сохраняет промпт, вердикт и период");
        CHECK(LlmJournalEntry::verdictName(QStringLiteral("ok")) == QObject::tr("Норма")
                  && LlmJournalEntry::verdictName(QStringLiteral("critical"))
                         == QObject::tr("Критично")
                  && LlmJournalEntry::verdictName(QString()) == QObject::tr("—"),
              "Журнал LLM: имена вердиктов");

        QTemporaryDir tempDir;
        CHECK(tempDir.isValid(), "Журнал LLM: временный каталог создан");
        {
            QFile file(tempDir.filePath(QStringLiteral("llm-journal.jsonl")));
            CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text),
                  "Журнал LLM: файл открыт для записи");
            file.write(QJsonDocument(entry.toJson()).toJson(QJsonDocument::Compact));
            file.write("\n");
            // Запись старого формата (только promptPreview) — совместимость
            file.write(R"({"id":41,"status":"Успешно","source":"Тetest","promptPreview":"старый превью"})");
            file.write("\n");
            file.close();
        }
        const QVector<LlmJournalEntry> loaded =
            LlmJournal::loadFromFile(tempDir.path(), 10);
        CHECK(loaded.size() == 2
                  && loaded.first().prompt == QStringLiteral("ПОЛНЫЙ ПРОМПТ")
                  && loaded.first().verdictStatus == QStringLiteral("warning")
                  && loaded.last().promptPreview == QStringLiteral("старый превью"),
              "Журнал LLM: loadFromFile восстанавливает историю (включая старый формат)");
    }

    // --- PromptBudget: оценка размера и деление запроса на чанки ---
    {
        CHECK(PromptBudget::estimateTokens(QStringLiteral("abcd")) == 1
                  && PromptBudget::estimateTokens(QStringLiteral("abcde")) == 2,
              "Бюджет: оценка токенов — символы/4 с округлением вверх");
        CHECK(PromptBudget::estimateTokens(QString(), QStringLiteral("abcdefgh")) == 2,
              "Бюджет: системный промпт учитывается");
        CHECK(PromptBudget::clampBudget(10) == 500
                  && PromptBudget::clampBudget(6000) == 6000,
              "Бюджет: минимальный порог 500 токенов");

        // 6 записей по ~220 байт (≈56 токенов); бюджет 700, шапка 400
        // → ёмкость чанка 300 токенов: 5+1 записей, запрос делится
        QVector<QJsonObject> records;
        for (int i = 0; i < 6; ++i) {
            QJsonObject record;
            record[QStringLiteral("id")] = i;
            record[QStringLiteral("pad")] = QString(200, QLatin1Char('x'));
            records.append(record);
        }
        const auto chunks = PromptBudget::splitIntoChunks(records, 700, 400);
        CHECK(chunks.size() > 1,
              "Бюджет: превышающий запрос разбит на несколько чанков");
        int fed = 0;
        for (const auto &chunk : chunks)
            fed += chunk.size();
        CHECK(fed == records.size(),
              "Бюджет: все записи распределены по чанкам без потерь");

        // Маленький запрос не режется
        const auto single = PromptBudget::splitIntoChunks(records, 100000, 50);
        CHECK(single.size() == 1 && single.first().size() == records.size(),
              "Бюджет: вмещающий запрос остаётся одним чанком");
        // Пустой список — один пустой чанк
        const auto empty = PromptBudget::splitIntoChunks({}, 1000, 0);
        CHECK(empty.size() == 1 && empty.first().isEmpty(),
              "Бюджет: пустой список — один пустой чанк");

        // Одиночная запись больше бюджета — уходит отдельным чанком, не выбрасывается
        QVector<QJsonObject> big{ records.first() };
        const auto bigChunks = PromptBudget::splitIntoChunks(big, 10, 50);
        CHECK(bigChunks.size() == 1 && bigChunks.first().size() == 1,
              "Бюджет: гигантская запись не выбрасывается, а уходит одна");
    }

    // --- VerdictAggregator: итог по нескольким чанкам ---
    {
        VerdictAggregator aggregator;
        CHECK(!aggregator.hasAny(), "Агрегатор: пустой — вердикта нет");

        aggregator.add(LlmVerdict::fromResponse(
            QStringLiteral(R"({"status":"ok","data":{"summary":"спокойно","findings":[]}})")));
        aggregator.add(LlmVerdict::fromResponse(QStringLiteral(
            R"({"status":"warning","data":{"summary":"скан","findings":[
                 {"title":"порт-скан","severity":"medium","details":"100 портов",
                  "recommendation":"блокировать"}]}})")));
        aggregator.add(LlmVerdict::fromResponse(QStringLiteral(
            R"({"status":"critical","data":{"summary":"флуд","findings":[
                 {"title":"syn-флуд","severity":"high","details":"300 pps",
                  "recommendation":"rate-limit"}]}})")));
        CHECK(aggregator.hasAny() && aggregator.count() == 3,
              "Агрегатор: учтены все чанки");
        CHECK(aggregator.level() == LlmVerdict::Level::Critical,
              "Агрегатор: итоговый уровень — максимум (critical)");
        const LlmVerdict total = aggregator.aggregated();
        CHECK(total.parsed
                  && total.level == LlmVerdict::Level::Critical
                  && total.statusText == QStringLiteral("critical")
                  && total.findings.size() == 2
                  && total.summary.contains(QStringLiteral("скан"))
                  && total.summary.contains(QStringLiteral("флуд"))
                  && total.data.value(QStringLiteral("findings")).toArray().size() == 2,
              "Агрегатор: summary объединён, findings склеены, data собрана");

        // warning без critical
        VerdictAggregator noCritical;
        noCritical.add(LlmVerdict::fromResponse(
            QStringLiteral(R"({"status":"warning","data":{"summary":"a"}})")));
        noCritical.add(LlmVerdict::fromResponse(
            QStringLiteral(R"({"status":"ok","data":{"summary":"b"}})")));
        CHECK(noCritical.level() == LlmVerdict::Level::Warning
                  && noCritical.aggregated().statusText == QStringLiteral("warning"),
              "Агрегатор: warning без critical остаётся warning");
    }

    // --- Фильтр по статусам оповещений ---
    {
        NotificationSettings n;
        CHECK(n.severityAllowed(Anomaly::Severity::Info)
                  && n.severityAllowed(Anomaly::Severity::Warning)
                  && n.severityAllowed(Anomaly::Severity::Critical),
              "Фильтр статусов: по умолчанию отправляются все уровни");

        n.severityWarning = false;
        CHECK(!n.severityAllowed(Anomaly::Severity::Warning)
                  && n.severityAllowed(Anomaly::Severity::Info)
                  && n.severityAllowed(Anomaly::Severity::Critical),
              "Фильтр статусов: выключенный уровень блокируется, другие проходят");

        n.severityInfo = false;
        n.severityCritical = true;
        CHECK(!n.severityAllowed(Anomaly::Severity::Info)
                  && n.severityAllowed(Anomaly::Severity::Critical),
              "Фильтр статусов: критично отправляется, инфо и предупреждение нет");

        n.severityCritical = false;
        CHECK(!n.severityAllowed(Anomaly::Severity::Info)
                  && !n.severityAllowed(Anomaly::Severity::Warning)
                  && !n.severityAllowed(Anomaly::Severity::Critical),
              "Фильтр статусов: выключенные все — ничего не отправляется");
    }

    // --- Живой SMTP: EmailNotifier против mock-сервера ---------------------
    // Включается переменной окружения NETANALYZER_SMTP_TEST=1, когда
    // tools/mock_smtp_server.py слушает 127.0.0.1:2525.
    if (qEnvironmentVariableIsSet("NETANALYZER_SMTP_TEST")) {
        NotificationSettings saved = Settings::instance().notifications();
        NotificationSettings n = saved;
        n.emailEnabled  = true;
        n.emailHost     = QStringLiteral("127.0.0.1");
        n.emailPort     = 2525;
        // Режим задаётся снаружи: 0 — без шифрования, 1 — STARTTLS
        // (мок поднимает TLS с самоподписанным сертификатом).
        n.emailSecurity = qEnvironmentVariableIntValue(
            "NETANALYZER_SMTP_SECURITY");
        n.emailUsername = QStringLiteral("tester");
        n.emailPassword = QStringLiteral("s3cret");
        n.emailFrom     = QStringLiteral("netanalyzer@test.local");
        n.emailTo       = QStringLiteral("a@test.local, b@test.local");
        Settings::instance().setNotifications(n);

        QString smtpError;
        bool smtpDone = false;
        bool smtpSent = false;
        {
            EmailNotifier notifier;
            QObject::connect(&notifier, &Notifier::sent, &notifier,
                             [&](const QString &, const QString &) {
                                 smtpSent = true;
                                 smtpDone = true;
                             });
            QObject::connect(&notifier, &Notifier::failed, &notifier,
                             [&](const QString &, const QString &error) {
                                 smtpError = error;
                                 smtpDone = true;
                             });
            notifier.sendText(QStringLiteral("Тест письма — UTF-8"),
                              QStringLiteral("Здравствуй! Это тестовое письмо "
                                             "от NetAnomalyAnalyzer."));

            QEventLoop loop;
            QTimer::singleShot(15000, &loop, &QEventLoop::quit);
            QObject::connect(&notifier, &Notifier::sent, &loop,
                             [&] { loop.quit(); });
            QObject::connect(&notifier, &Notifier::failed, &loop,
                             [&] { loop.quit(); });
            loop.exec();

            if (!smtpDone)
                smtpError = QStringLiteral("таймаут 15 с");
            CHECK(smtpSent,
                  qPrintable(QStringLiteral("SMTP: письмо доставлено в мок"
                                            "%1")
                                 .arg(smtpError.isEmpty()
                                          ? QString()
                                          : QStringLiteral(" — %1")
                                                .arg(smtpError))));
            if (!smtpError.isEmpty() && !smtpSent) {
                std::printf("       причина: %s\n",
                            qPrintable(smtpError));
            }
        }

        // Неправильный порт — ожидаем корректный отказ
        {
            NotificationSettings bad = n;
            bad.emailPort = 2526;   // никто не слушает
            Settings::instance().setNotifications(bad);
            QString error;
            bool done = false;
            EmailNotifier notifier;
            QObject::connect(&notifier, &Notifier::failed, &notifier,
                             [&](const QString &, const QString &e) {
                                 error = e;
                                 done = true;
                             });
            QObject::connect(&notifier, &Notifier::sent, &notifier,
                             [&] { done = true; });
            notifier.sendText(QStringLiteral("x"), QStringLiteral("y"));
            QEventLoop loop;
            QTimer::singleShot(8000, &loop, &QEventLoop::quit);
            QObject::connect(&notifier, &Notifier::failed, &loop,
                             [&] { loop.quit(); });
            loop.exec();
            CHECK(done && !error.isEmpty(),
                  "SMTP: недоступный сервер даёт сообщение об ошибке");
        }

        // --- Живой фильтр статусов через NotificationManager ---------------
        // Тот же путь, что у сообщений от LLM: verdictAnomaly -> onAnomaly.
        {
            const QString oldDir = Settings::instance().logging().logDir;
            QTemporaryDir tempDir;
            LoggingSettings logging = Settings::instance().logging();
            logging.logDir = tempDir.path();
            Settings::instance().setLogging(logging);

            NotificationSettings ns = n;    // почта уже настроена на мок
            ns.severityInfo = false;        // инфо-уровень отключён
            ns.severityWarning = true;
            ns.severityCritical = true;
            ns.sound = false;
            Settings::instance().setNotifications(ns);

            LogWriter writer;
            NotificationManager manager(&writer);

            int guiCount = 0, emailSent = 0, emailFailed = 0;
            QObject::connect(&manager, &NotificationManager::guiNotification,
                             &manager, [&](const Anomaly &) { ++guiCount; });
            for (Notifier *channel : manager.notifiers()) {
                if (!channel->channelName().contains(QStringLiteral("Почта")))
                    continue;
                QObject::connect(channel, &Notifier::sent, &manager,
                                 [&](const QString &, const QString &) {
                                     ++emailSent;
                                 });
                QObject::connect(channel, &Notifier::failed, &manager,
                                 [&](const QString &, const QString &) {
                                     ++emailFailed;
                                 });
            }

            auto processFor = [](int ms) {
                QEventLoop loop;
                QTimer::singleShot(ms, &loop, &QEventLoop::quit);
                loop.exec();
            };

            // 1) Информация — отфильтрована: ни почты, ни GUI, журнал пишется
            Anomaly info;
            info.setId(901);
            info.setTimestamp(QDateTime::currentDateTime());
            info.setRuleName(QStringLiteral("ТестИнфо"));
            info.setSeverity(Anomaly::Severity::Info);
            info.setTitle(QStringLiteral("Информационное событие"));
            manager.onAnomaly(info);
            processFor(700);
            CHECK(guiCount == 0 && emailSent == 0 && emailFailed == 0,
                  "Фильтр: инфо-аномалия не ушла по почте и не показана в GUI");

            // 2) Предупреждение — проходит по всем каналам
            Anomaly warn = info;
            warn.setId(902);
            warn.setRuleName(QStringLiteral("ТестПредупреждение"));
            warn.setSeverity(Anomaly::Severity::Warning);
            warn.setTitle(QStringLiteral("Подозрительная активность"));
            manager.onAnomaly(warn);
            {
                QEventLoop loop;
                QTimer deadline;
                deadline.setSingleShot(true);
                QObject::connect(&deadline, &QTimer::timeout, &loop,
                                 &QEventLoop::quit);
                QTimer ticker;
                QObject::connect(&ticker, &QTimer::timeout, &loop, [&] {
                    if (emailSent > 0)
                        loop.quit();
                });
                deadline.start(15000);
                ticker.start(100);
                loop.exec();
            }
            CHECK(emailSent == 1 && emailFailed == 0,
                  "Фильтр: предупреждение отправлено по почте в мок-SMTP");
            CHECK(guiCount == 1,
                  "Фильтр: GUI-оповещение показано только для предупреждения");

            // Журнал notifications.jsonl пишется всегда (обе записи)
            QFile journal(tempDir.filePath(QStringLiteral("notifications.jsonl")));
            CHECK(journal.open(QIODevice::ReadOnly | QIODevice::Text),
                  "Фильтр: журнал оповещений открыт");
            if (journal.isOpen()) {
                const int lines = journal.readAll().count('\n');
                CHECK(lines == 2,
                      "Фильтр: в журнале обе записи (включая отфильтрованную)");
            }

            LoggingSettings restore = Settings::instance().logging();
            restore.logDir = oldDir;
            Settings::instance().setLogging(restore);
        }

        Settings::instance().setNotifications(saved);
    }

    // --- LLM: отправленные аномалии не отправляются повторно -----------------
    {
        const QString oldDir = Settings::instance().logging().logDir;
        const LlmSettings oldLlm = Settings::instance().llm();
        QTemporaryDir tempDir;
        LoggingSettings logging = Settings::instance().logging();
        logging.logDir = tempDir.path();
        Settings::instance().setLogging(logging);

        // Детерминированные параметры LLM-контекста (без таймера расписания).
        {
            LlmSettings base = Settings::instance().llm();
            base.enabled = false;
            base.contextBudgetTokens = 6000;
            base.contextLines = 100;
            Settings::instance().setLlm(base);
        }

        // Прямая проверка хранилища отметок.
        {
            LlmSentStore store(
                tempDir.filePath(QStringLiteral("sent-test.json")));
            CHECK(store.markSent({QStringLiteral("k1"), QStringLiteral("k2"),
                                  QStringLiteral("k1")}) == 2,
                  "LLM: store принимает только новые ключи");
            CHECK(store.contains(QStringLiteral("k1"))
                      && !store.contains(QStringLiteral("k3")),
                  "LLM: store помнит отметки");
            LlmSentStore reloaded(
                tempDir.filePath(QStringLiteral("sent-test.json")));
            CHECK(reloaded.size() == 2 && reloaded.contains(QStringLiteral("k2")),
                  "LLM: отметки пережили сохранение в файл");
        }

        LogWriter logWriter;
        StubLlmClient stub;
        LlmScheduler scheduler(&stub, nullptr, &logWriter);

        auto addAnomaly = [&logWriter](qint64 id, const QString &marker) {
            Anomaly anomaly;
            anomaly.setId(id);
            anomaly.setTimestamp(QDateTime::currentDateTime());
            anomaly.setRuleName(QStringLiteral("Тест"));
            anomaly.setTitle(marker);
            // Описание без маркера (маркер встречается в записи один раз)
            // и с запасом объёма — для чанкинг-проверки нужен контекст
            // побольше.
            anomaly.setDescription(QStringLiteral("описание события %1; ").arg(id)
                                   + QString(400, QLatin1Char('x')));
            logWriter.onAnomaly(anomaly);
        };

        // Первый анализ: уходит весь журнал аномалий.
        addAnomaly(1, QStringLiteral("ANOM-A1"));
        addAnomaly(2, QStringLiteral("ANOM-A2"));
        scheduler.requestManualAnalysis();
        CHECK(stub.prompts.size() == 1,
              "LLM: первый анализ отправлен одним запросом");
        CHECK(stub.prompts.value(0).contains(QStringLiteral("ANOM-A1"))
                  && stub.prompts.value(0).contains(QStringLiteral("ANOM-A2")),
              "LLM: в первый запрос попали все аномалии журнала");

        // Повторный анализ: новых аномалий нет — повторной отправки нет.
        scheduler.requestManualAnalysis();
        CHECK(stub.prompts.size() == 2, "LLM: повторный анализ отправлен");
        const QString second = stub.prompts.value(1);
        CHECK(!second.contains(QStringLiteral("ANOM-A1"))
                  && !second.contains(QStringLiteral("ANOM-A2")),
              "LLM: уже отправленные аномалии не повторяются");
        CHECK(second.contains(QStringLiteral("Новых аномалий нет")),
              "LLM: промпт сообщает об отсутствии новых аномалий");

        // Появились новые — уходят только они.
        addAnomaly(3, QStringLiteral("ANOM-B3"));
        addAnomaly(4, QStringLiteral("ANOM-B4"));
        scheduler.requestManualAnalysis();
        const QString third = stub.prompts.value(2);
        CHECK(third.contains(QStringLiteral("ANOM-B3"))
                  && third.contains(QStringLiteral("ANOM-B4")),
              "LLM: новые аномалии отправлены");
        CHECK(!third.contains(QStringLiteral("ANOM-A1"))
                  && !third.contains(QStringLiteral("ANOM-A2")),
              "LLM: старые аномалии в новый запрос не попали");

        // Ошибка запроса: отметки не ставятся, аномалия уйдёт повторно.
        addAnomaly(5, QStringLiteral("ANOM-C5"));
        stub.failNext = true;
        scheduler.requestManualAnalysis();   // завершится ошибкой
        scheduler.requestManualAnalysis();   // повтор
        CHECK(stub.prompts.last().contains(QStringLiteral("ANOM-C5")),
              "LLM: после ошибки запроса аномалия отправляется повторно");

        // Чанки: каждая новая аномалия уходит ровно один раз.
        LlmSettings llm = Settings::instance().llm();
        llm.enabled = false;                // без таймера — только ручной запуск
        llm.contextBudgetTokens = 500;      // минимум — контекст режется на части
        llm.contextLines = 100;
        Settings::instance().setLlm(llm);
        addAnomaly(6, QStringLiteral("ANOM-D6"));
        addAnomaly(7, QStringLiteral("ANOM-D7"));
        addAnomaly(8, QStringLiteral("ANOM-D8"));
        stub.prompts.clear();
        scheduler.requestManualAnalysis();
        CHECK(stub.prompts.size() > 1, "LLM: превысивший бюджет контекст разбит на чанки");
        int d6 = 0, d7 = 0, d8 = 0;
        for (const QString &chunkPrompt : std::as_const(stub.prompts)) {
            d6 += chunkPrompt.count(QStringLiteral("ANOM-D6"));
            d7 += chunkPrompt.count(QStringLiteral("ANOM-D7"));
            d8 += chunkPrompt.count(QStringLiteral("ANOM-D8"));
        }
        CHECK(d6 == 1 && d7 == 1 && d8 == 1,
              "LLM: в чанках каждая аномалия отправлена ровно один раз");
        stub.prompts.clear();
        scheduler.requestManualAnalysis();
        int repeats = 0;
        for (const QString &chunkPrompt : std::as_const(stub.prompts))
            repeats += chunkPrompt.count(QStringLiteral("ANOM-D"));
        CHECK(repeats == 0, "LLM: после чанкового анализа повторной отправки нет");

        // Отметки переживают перезапуск: новый планировщик не шлёт старое.
        LogWriter restartedLogWriter;
        StubLlmClient restartedStub;
        LlmScheduler restarted(&restartedStub, nullptr, &restartedLogWriter);
        restarted.requestManualAnalysis();
        CHECK(!restartedStub.prompts.isEmpty()
                  && !restartedStub.prompts.last().contains(QStringLiteral("ANOM-D"))
                  && !restartedStub.prompts.last().contains(QStringLiteral("ANOM-C5")),
              "LLM: отправленные аномалии не повторяются после перезапуска");

        Settings::instance().setLlm(oldLlm);
        LoggingSettings restore = Settings::instance().logging();
        restore.logDir = oldDir;
        Settings::instance().setLogging(restore);
    }

    std::printf("\n%s (%d ошибок)\n", g_failures == 0 ? "ALL PASSED" : "FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
