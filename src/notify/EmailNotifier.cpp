#include "EmailNotifier.h"

#include <QDateTime>
#include <QRegularExpression>

namespace {
/** Обернуть base64 по 76 символов (RFC 2045). */
QByteArray wrapBase64(const QByteArray &data)
{
    const QByteArray b64 = data.toBase64();
    QByteArray wrapped;
    for (int i = 0; i < b64.size(); i += 76)
        wrapped += b64.mid(i, 76) + "\r\n";
    return wrapped;
}
} // namespace

EmailNotifier::EmailNotifier(QObject *parent)
    : Notifier(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(tr("таймаут SMTP-сервера"));
    });

    connect(&m_socket, &QSslSocket::readyRead, this, [this] {
        m_buffer += m_socket.readAll();
        int nl = -1;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            QByteArray line = m_buffer.left(nl);
            m_buffer.remove(0, nl + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            if (m_state == State::Idle)
                return;
            processLine(line);
        }
    });
    connect(&m_socket, &QSslSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                if (m_state != State::Idle)
                    fail(m_socket.errorString());
            });
    connect(&m_socket, &QSslSocket::encrypted, this, [this] {
        if (m_state == State::WaitStartTls) {
            m_state = State::WaitEhloAfterTls;
            sendLine("EHLO netanalyzer");
        }
    });
    // Допускаем самоподписанные сертификаты внутренних почтовых серверов:
    // соединение остаётся зашифрованным, просто не проверяется владелец.
    connect(&m_socket, &QSslSocket::sslErrors, this,
            [this] { m_socket.ignoreSslErrors(); });
}

bool EmailNotifier::isEnabled() const
{
    const NotificationSettings &s = Settings::instance().notifications();
    return s.emailEnabled
           && !s.emailHost.trimmed().isEmpty()
           && !s.emailFrom.trimmed().isEmpty()
           && !s.emailTo.trimmed().isEmpty();
}

void EmailNotifier::notify(const Anomaly &anomaly)
{
    const QString subject = QStringLiteral("[NetAnomalyAnalyzer] [%1] %2")
                                .arg(anomaly.severityName(), anomaly.title());
    QString body = anomaly.notificationText();
    body += QStringLiteral("\nВремя: %1")
                .arg(anomaly.timestamp().toString(Qt::ISODate));
    if (!anomaly.interfaceName().isEmpty())
        body += QStringLiteral("\nИнтерфейс: %1").arg(anomaly.interfaceName());
    if (!anomaly.description().isEmpty())
        body += QStringLiteral("\n\n%1").arg(anomaly.description());
    sendText(subject, body);
}

void EmailNotifier::sendText(const QString &subject, const QString &body)
{
    if (!isEnabled()) {
        emit failed(channelName(), tr("Почта не настроена"));
        return;
    }
    beginSend(subject, body);
}

void EmailNotifier::beginSend(const QString &subject, const QString &body)
{
    if (m_state != State::Idle) {
        emit failed(channelName(), tr("предыдущая отправка ещё выполняется"));
        return;
    }

    const NotificationSettings &s = Settings::instance().notifications();

    // Тело письма
    QByteArray message;
    message += "From: " + s.emailFrom.trimmed().toUtf8() + "\r\n";
    message += "To: " + recipients().join(QStringLiteral(", ")).toUtf8() + "\r\n";
    message += "Subject: =?UTF-8?B?" + subject.toUtf8().toBase64() + "?=\r\n";
    message += "Date: " + QDateTime::currentDateTime().toString(Qt::RFC2822Date)
                      .toUtf8() + "\r\n";
    message += "MIME-Version: 1.0\r\n";
    message += "Content-Type: text/plain; charset=utf-8\r\n";
    message += "Content-Transfer-Encoding: base64\r\n";
    message += "\r\n";
    message += wrapBase64(body.toUtf8());
    m_message = message;

    m_pendingRcpt = recipients();
    m_startTlsRequested = false;
    m_authStarted = false;
    m_buffer.clear();
    m_state = State::WaitGreeting;

    m_timeout.start(30000);
    if (s.emailSecurity == 2)
        m_socket.connectToHostEncrypted(s.emailHost.trimmed(),
                                         static_cast<quint16>(s.emailPort));
    else
        m_socket.connectToHost(s.emailHost.trimmed(),
                               static_cast<quint16>(s.emailPort));
}

void EmailNotifier::sendLine(const QByteArray &line)
{
    m_socket.write(line + "\r\n");
}

void EmailNotifier::processLine(const QByteArray &line)
{
    // Ответ сервера: три цифры кода + пробел (конец многострочного) или
    // минус (продолжение). Нестандартные строки игнорируем.
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (line.size() < 4 || !digit(line[0]) || !digit(line[1])
        || !digit(line[2]))
        return;
    const int code = line.left(3).toInt();
    const bool lastLine = line[3] == ' ';

    switch (m_state) {
    case State::Idle:
        return;

    case State::WaitGreeting:
        if (code != 220)
            return fail(tr("сервер не представился (код %1)").arg(code));
        m_state = State::WaitEhlo;
        sendLine("EHLO netanalyzer");
        return;

    case State::WaitEhlo:
    case State::WaitEhloAfterTls:
        if (!lastLine)
            return;   // продолжение многострочного 250-
        if (code != 250)
            return fail(tr("EHLO отклонён (код %1)").arg(code));
        afterEhlo();
        return;

    case State::WaitStartTls:
        if (code != 220)
            return fail(tr("STARTTLS отклонён (код %1)").arg(code));
        m_socket.startClientEncryption();
        return;

    case State::WaitAuthUser:
        if (code != 334)
            return fail(tr("AUTH LOGIN отклонён (код %1)").arg(code));
        m_state = State::WaitAuthPass;
        sendLine(Settings::instance().notifications().emailUsername
                     .trimmed().toUtf8().toBase64());
        return;

    case State::WaitAuthPass:
        if (code != 334)
            return fail(tr("логин отклонён (код %1)").arg(code));
        m_state = State::WaitAuthResult;
        sendLine(Settings::instance().notifications().emailPassword
                     .toUtf8().toBase64());
        return;

    case State::WaitAuthResult:
        if (code != 235)
            return fail(tr("авторизация не пройдена (код %1)").arg(code));
        m_state = State::WaitMailFrom;
        sendLine("MAIL FROM:<"
                 + Settings::instance().notifications().emailFrom
                       .trimmed().toUtf8()
                 + ">");
        return;

    case State::WaitMailFrom:
        if (code != 250)
            return fail(tr("MAIL FROM отклонён (код %1)").arg(code));
        if (m_pendingRcpt.isEmpty())
            return fail(tr("нет получателей"));
        m_state = State::WaitRcpt;
        sendLine("RCPT TO:<" + m_pendingRcpt.takeFirst().toUtf8() + ">");
        return;

    case State::WaitRcpt:
        if (code != 250 && code != 251)
            return fail(tr("получатель отклонён (код %1)").arg(code));
        if (!m_pendingRcpt.isEmpty()) {
            sendLine("RCPT TO:<" + m_pendingRcpt.takeFirst().toUtf8() + ">");
            return;
        }
        m_state = State::WaitDataCmd;
        sendLine("DATA");
        return;

    case State::WaitDataCmd:
        if (code != 354)
            return fail(tr("DATA отклонён (код %1)").arg(code));
        m_state = State::WaitMessage;
        m_socket.write(m_message + "\r\n.\r\n");
        return;

    case State::WaitMessage:
        if (code != 250)
            return fail(tr("сервер не принял письмо (код %1)").arg(code));
        succeed();
        return;
    }
}

void EmailNotifier::afterEhlo()
{
    const NotificationSettings &s = Settings::instance().notifications();

    // STARTTLS выполняется один раз: после шифрования EHLO повторяется.
    if (s.emailSecurity == 1 && !m_startTlsRequested) {
        m_startTlsRequested = true;
        m_state = State::WaitStartTls;
        sendLine("STARTTLS");
        return;
    }
    if (!s.emailUsername.trimmed().isEmpty() && !m_authStarted) {
        m_authStarted = true;
        m_state = State::WaitAuthUser;
        sendLine("AUTH LOGIN");
        return;
    }
    m_state = State::WaitMailFrom;
    sendLine("MAIL FROM:<" + s.emailFrom.trimmed().toUtf8() + ">");
}

void EmailNotifier::fail(const QString &error)
{
    m_timeout.stop();
    m_state = State::Idle;
    m_socket.abort();
    emit failed(channelName(), error);
}

void EmailNotifier::succeed()
{
    m_timeout.stop();
    m_state = State::Idle;
    sendLine("QUIT");
    m_socket.disconnectFromHost();
    emit sent(channelName(), tr("доставлено"));
}

QStringList EmailNotifier::recipients() const
{
    const QString raw = Settings::instance().notifications().emailTo;
    QStringList result;
    const QStringList parts =
        raw.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString addr = part.trimmed();
        if (!addr.isEmpty())
            result.append(addr);
    }
    return result;
}
