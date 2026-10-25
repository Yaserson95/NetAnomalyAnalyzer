#pragma once

#include "Notifier.h"
#include "../settings/Settings.h"

#include <QSslSocket>
#include <QTimer>

/**
 * @brief Оповещение на электронную почту по SMTP.
 *
 * Qt не включает модуль SMTP, поэтому здесь реализован минимальный
 * клиент протокола SMTP поверх QSslSocket: приветствие, EHLO,
 * опционально STARTTLS/SSL, авторизация AUTH LOGIN, MAIL FROM,
 * RCPT TO, DATA. Сообщение — UTF-8 с кодировкой заголовков RFC 2047
 * и телом base64 (Content-Type: text/plain; charset=utf-8).
 *
 * Ответ по сигналам sent()/failed() базового Notifier.
 */
class EmailNotifier : public Notifier {
    Q_OBJECT
public:
    explicit EmailNotifier(QObject *parent = nullptr);

    QString channelName() const override { return tr("Почта"); }
    bool isEnabled() const override;
    void notify(const Anomaly &anomaly) override;

    /** Отправить произвольное письмо (кнопка «Отправить тест»). */
    void sendText(const QString &subject, const QString &body);

private:
    enum class State {
        Idle,
        WaitGreeting,     ///< ждём 220 после подключения
        WaitEhlo,         ///< ждём многострочный 250 на EHLO
        WaitStartTls,     ///< ждём 220 на STARTTLS
        WaitEhloAfterTls, ///< EHLO после шифрования
        WaitAuthUser,     ///< 334 после AUTH LOGIN
        WaitAuthPass,     ///< 334 после логина
        WaitAuthResult,   ///< 235 после пароля
        WaitMailFrom,     ///< 250 на MAIL FROM
        WaitRcpt,         ///< 250 на RCPT TO
        WaitDataCmd,      ///< 354 на DATA
        WaitMessage       ///< 250 на тело письма
    };

    void beginSend(const QString &subject, const QString &body);
    void sendLine(const QByteArray &line);
    void processLine(const QByteArray &line);
    void afterEhlo();
    void fail(const QString &error);
    void succeed();

    QStringList recipients() const;

    QSslSocket m_socket;
    QTimer     m_timeout;
    QByteArray m_buffer;
    State      m_state = State::Idle;
    bool       m_startTlsRequested = false;  ///< уже выставили STARTTLS
    bool       m_authStarted = false;
    QStringList m_pendingRcpt;
    QByteArray m_message;
};
