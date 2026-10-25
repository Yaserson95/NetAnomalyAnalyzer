"""Мок SMTP-сервера для проверки EmailNotifier.

Слушает 127.0.0.1:2525, ведёт полный диалог SMTP (EHLO, AUTH LOGIN,
MAIL FROM, RCPT TO, DATA), сохраняет принятое письмо в файл.
Если установлен пакет cryptography — генерирует самоподписанный
сертификат и поддерживает STARTTLS (клиент игнорирует ошибки сертификата).

Запуск:
    python tools/mock_smtp_server.py [путь_для_записи_письма] [порт]

Проверка EmailNotifier выполняется переменными окружения:
    NETANALYZER_SMTP_TEST=1              — включает SMTP-тесты
    NETANALYZER_SMTP_SECURITY=1          — режим STARTTLS (по умолчанию 0)
    build\\FilterTest.exe
"""
import base64
import os
import socket
import ssl
import sys
import threading

HOST = "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 2525
capture_path = sys.argv[1] if len(sys.argv) > 1 else "mock_smtp_captured.eml"
lock = threading.Lock()


def make_ssl_context():
    """Самоподписанный сертификат для STARTTLS (если cryptography доступен)."""
    cert_file = capture_path + ".cert.pem"
    key_file = capture_path + ".key.pem"
    if not (os.path.exists(cert_file) and os.path.exists(key_file)):
        try:
            from cryptography import x509
            from cryptography.hazmat.primitives import hashes, serialization
            from cryptography.hazmat.primitives.asymmetric import rsa
            from cryptography.x509.oid import NameOID
            import datetime
            key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
            name = x509.Name([
                x509.NameAttribute(NameOID.COMMON_NAME, "mock-smtp.local")])
            cert = (
                x509.CertificateBuilder()
                .subject_name(name).issuer_name(name)
                .public_key(key.public_key()).serial_number(
                    x509.random_serial_number())
                .not_valid_before(datetime.datetime.utcnow() -
                                  datetime.timedelta(days=1))
                .not_valid_after(datetime.datetime.utcnow() +
                                 datetime.timedelta(days=365))
                .add_extension(x509.SubjectAlternativeName(
                    [x509.DNSName("localhost"),
                     x509.IPAddress(__import__("ipaddress").ip_address(
                         "127.0.0.1"))]), critical=False)
                .sign(key, hashes.SHA256()))
            with open(cert_file, "wb") as fh:
                fh.write(cert.public_bytes(serialization.Encoding.PEM))
            with open(key_file, "wb") as fh:
                fh.write(key.private_bytes(
                    serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8,
                    serialization.NoEncryption()))
            print(f"self-signed cert -> {cert_file}", flush=True)
        except ImportError:
            print("cryptography not installed — STARTTLS disabled", flush=True)
            return None
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert_file, key_file)
    return ctx


SSL_CONTEXT = None  # инициализируется в main()


def handle(conn: socket.socket) -> None:
    sock = conn
    stream = sock.makefile("rb")

    def reply(line: bytes) -> None:
        sock.sendall(line + b"\r\n")

    reply(b"220 mock SMTP ready")
    auth_stage = 0          # 0 — нет, 1 — ждём логин, 2 — ждём пароль
    in_data = False
    data_lines = []

    while True:
        raw = stream.readline()
        if not raw:
            break
        line = raw.rstrip(b"\r\n")

        if in_data:
            if line == b".":
                in_data = False
                with lock:
                    with open(capture_path, "wb") as fh:
                        fh.write(b"\r\n".join(data_lines))
                reply(b"250 2.0.0 Ok: queued")
            else:
                data_lines.append(line)
            continue

        upper = line.upper()
        if upper.startswith(b"EHLO"):
            reply(b"250-mock SMTP\r\n"
                  b"250-AUTH LOGIN\r\n"
                  b"250-STARTTLS\r\n"
                  b"250-SIZE 10485760\r\n"
                  b"250 8BITMIME")
        elif upper.startswith(b"HELO"):
            reply(b"250 mock")
        elif upper.startswith(b"STARTTLS"):
            if SSL_CONTEXT is None:
                reply(b"454 4.7.0 TLS not available")
                continue
            reply(b"220 2.0.0 Ready to start TLS")
            try:
                stream.close()
                sock = SSL_CONTEXT.wrap_socket(sock, server_side=True)
            except ssl.SSLError as exc:
                print(f"TLS handshake failed: {exc}", flush=True)
                break
            stream = sock.makefile("rb")
            print("STARTTLS handshake completed", flush=True)
        elif upper.startswith(b"AUTH LOGIN"):
            auth_stage = 1
            reply(b"334 VXNlcm5hbWU6")          # "Username:"
        elif auth_stage == 1:
            user = base64.b64decode(line).decode("utf-8", "replace")
            with lock:
                with open(capture_path + ".auth", "w", encoding="utf-8") as fh:
                    fh.write(user)
            auth_stage = 2
            reply(b"334 UGFzc3dvcmQ6")          # "Password:"
        elif auth_stage == 2:
            password = base64.b64decode(line).decode("utf-8", "replace")
            with lock:
                with open(capture_path + ".auth", "a", encoding="utf-8") as fh:
                    fh.write("\n" + password)
            auth_stage = 0
            reply(b"235 2.7.0 Authentication successful")
        elif upper.startswith(b"MAIL FROM"):
            reply(b"250 2.1.0 Ok")
        elif upper.startswith(b"RCPT TO"):
            reply(b"250 2.1.5 Ok")
        elif upper.startswith(b"DATA"):
            in_data = True
            data_lines = []
            reply(b"354 End data with <CR><LF>.<CR><LF>")
        elif upper.startswith(b"QUIT"):
            reply(b"221 2.0.0 Bye")
            break
        else:
            reply(b"502 5.5.2 Command not implemented")

    try:
        sock.close()
    except OSError:
        pass


def main() -> None:
    global SSL_CONTEXT
    SSL_CONTEXT = make_ssl_context()
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(5)
    print(f"mock SMTP listening on {HOST}:{PORT}, capture -> {capture_path}",
          flush=True)
    while True:
        conn, _ = server.accept()
        threading.Thread(target=handle, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
