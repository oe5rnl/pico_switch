Import("env")
import subprocess

# Manuelle Hauptversion (xx); muss zum Pico-FW_MAJOR passen, wenn gewuenscht.
ESP_FW_MAJOR = "01"

try:
    count = subprocess.check_output(
        ["git", "rev-list", "--count", "HEAD"],
        cwd=env["PROJECT_DIR"],
    ).decode().strip()
except Exception:
    count = "0"

if not count.isdigit():
    count = "0"

version = "%s.%s" % (ESP_FW_MAJOR, count.zfill(5))
env.Append(CPPDEFINES=[("ESP_FW_VERSION", env.StringifyMacro(version))])
