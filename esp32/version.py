Import("env")
import subprocess

# Manuelle Hauptversion (xx); muss zum Pico-FW_MAJOR passen, wenn gewuenscht.
ESP_FW_MAJOR = "01"

def git(args, default=""):
    try:
        return subprocess.check_output(
            ["git"] + args, cwd=env["PROJECT_DIR"]
        ).decode().strip()
    except Exception:
        return default

count = git(["rev-list", "--count", "HEAD"], "0")
if not count.isdigit():
    count = "0"

short = git(["rev-parse", "--short=7", "HEAD"], "0000000") or "0000000"

# Nur getrackte Aenderungen zaehlen als "dirty" (wie git describe --dirty).
dirty = "-dirty" if git(["status", "--porcelain", "--untracked-files=no"]) else ""

version = "%s.%s.g%s%s" % (ESP_FW_MAJOR, count.zfill(5), short, dirty)
env.Append(CPPDEFINES=[("ESP_FW_VERSION", env.StringifyMacro(version))])

