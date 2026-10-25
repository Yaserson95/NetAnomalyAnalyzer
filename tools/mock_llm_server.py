"""Мок OpenAI-совместимого API для проверки LLM-подсистемы NetAnomalyAnalyzer."""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(length) or b"{}")
        messages = body.get("messages", [])
        user = next((m.get("content", "") for m in messages if m.get("role") == "user"), "")
        # Ответ в требуемом формате {"status":…,"data":{…}}
        reply = json.dumps({
            "status": "warning",
            "data": {
                "summary": ("МОК-АНАЛИЗ: зафиксировано сканирование портов и SYN-флуд — "
                            f"вероятна разведка/атака DoS (получено символов промпта: {len(user)})."),
                "findings": [
                    {"title": "Порт-скан", "severity": "high",
                     "details": "Источник прошёл по множеству портов назначения.",
                     "recommendation": "Ограничить источник на межсетевом экране, включить логирование."},
                    {"title": "SYN-флуд", "severity": "medium",
                     "details": "Массовые SYN-пакеты без завершения handshake.",
                     "recommendation": "Включить фильтрацию SYN и rate-limiting."},
                ],
            },
        }, ensure_ascii=False)
        payload = json.dumps({
            "id": "mock-1",
            "object": "chat.completion",
            "model": body.get("model", "mock"),
            "choices": [{"index": 0, "message": {"role": "assistant", "content": reply},
                         "finish_reason": "stop"}],
        }).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):
        pass


HTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
