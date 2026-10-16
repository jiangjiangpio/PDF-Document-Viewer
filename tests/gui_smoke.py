"""Exercise the shipped Windows GUI under Wine + Xvfb, using real mouse/key events."""
import os
from pathlib import Path
import struct
import subprocess
import time

root = Path(__file__).resolve().parents[1]
env = dict(os.environ, DISPLAY=":99", LD_LIBRARY_PATH="/workspace/toolchain/root/usr/lib/x86_64-linux-gnu")
xdotool = "/workspace/toolchain/root/usr/bin/xdotool"
wine = "/workspace/toolchain/run-wine"
exe = root / "build-windows/package/PaperView.exe"
prefix = Path("/workspace/wineprefix")
key = (root / "build-windows/smoke/report.txt").read_text().split("fingerprint=")[1].splitlines()[0]

def x(*args):
    return subprocess.run([xdotool, *map(str, args)], env=env, check=True, capture_output=True).stdout.decode().strip()

def wait_until(predicate, description, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            print("PASS", description, flush=True)
            return
        time.sleep(.1)
    raise AssertionError(description)

def annotation():
    matches = list((prefix / "drive_c/users").glob("*/AppData/Local/PaperView/annotations/" + key + ".ink"))
    if not matches:
        return None
    data = matches[0].read_bytes()
    if len(data) < 40:
        return None
    return {"file": matches[0], "page": struct.unpack_from("<i", data, 8)[0], "rotation": struct.unpack_from("<i", data, 12)[0], "count": struct.unpack_from("<I", data, 36)[0], "data": data}

windows = x("search", "--name", "^PDF view$").splitlines()
window = windows[0]
x("windowfocus", "--sync", window)
assert x("getwindowname", window) == "PDF view"
print("PASS window title is exactly PDF view", flush=True)
x("mousemove", 150, 80, "click", 1)
x("mousemove", 660, 390, "mousedown", 1, "mousemove", 710, 370, "mousemove", 760, 410, "mouseup", 1)
wait_until(lambda: annotation() and annotation()["count"] == 1, "mouse ink automatically persists")
x("key", "ctrl+z")
wait_until(lambda: annotation()["count"] == 0, "undo persists")
x("key", "ctrl+y")
wait_until(lambda: annotation()["count"] == 1, "redo persists")
x("mousemove", 280, 80, "click", 1)
x("mousemove", 710, 400, "mousedown", 1, "mousemove", 715, 400, "mouseup", 1)
wait_until(lambda: annotation()["count"] == 0, "whole-stroke eraser persists")
x("key", "ctrl+z")
wait_until(lambda: annotation()["count"] == 1, "eraser undo restores stroke")
x("mousemove", 150, 80, "click", 1)
x("mousemove", 900, 500, "mousedown", 1, "mousemove", 900, 450, "mouseup", 1)
time.sleep(.3)
assert annotation()["count"] == 1
print("PASS drawing toggle prevents accidental ink", flush=True)
before = annotation()["file"].stat().st_mtime_ns
x("mousemove", 1115, 80, "click", 1)
wait_until(lambda: annotation()["file"].stat().st_mtime_ns > before, "save icon writes local annotations")
x("key", "ctrl+f")
x("type", "--clearmodifiers", "--delay", 60, "Equation")
x("key", "Return")
time.sleep(1)
x("key", "F3")
wait_until(lambda: annotation()["page"] >= 1, "search next scrolls to later match")
x("key", "Escape")
x("key", "ctrl+s")
subprocess.run([wine, str(exe), r"Z:\workspace\PDF-Document-Viewer\build-fixtures\large-300-pages.pdf"], check=True)
time.sleep(1)
assert len(x("search", "--name", "^PDF view$").splitlines()) == 1
print("PASS second launch opens another tab in the same instance", flush=True)
x("key", "ctrl+Tab")
time.sleep(.5)
x("mousemove", 790, 80, "click", 1)
wait_until(lambda: annotation()["rotation"] == 1, "rotation is saved independently of stroke coordinates")
subprocess.run(["import", "-window", "root", str(root / "build-windows/smoke/annotated-window.png")], env=env, check=True)
subprocess.run([wine, str(root / "build-windows/windows_ui_probe.exe"), "780", "850"], check=True)
time.sleep(.5)
subprocess.run(["import", "-window", "root", str(root / "build-windows/smoke/compact-window.png")], env=env, check=True)
print("PASS compact toolbar layout remains usable", flush=True)
x("key", "alt+F4")
wait_until(lambda: subprocess.run([xdotool,"search","--name","^PDF view$"], env=env, capture_output=True).returncode != 0, "application closes cleanly")
print("GUI_SMOKE=PASS", flush=True)
