import signal
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

exe = r"build\NetAnalyzerService.exe"
p = subprocess.Popen(
    [exe, "--service", "--no-capture"],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
)
time.sleep(4)
p.send_signal(signal.CTRL_BREAK_EVENT)
try:
    out, _ = p.communicate(timeout=15)
    rc = p.returncode
except subprocess.TimeoutExpired:
    p.kill()
    out, _ = p.communicate()
    rc = -999
print("returncode =", rc)
print(out.decode("utf-8", "replace"))
