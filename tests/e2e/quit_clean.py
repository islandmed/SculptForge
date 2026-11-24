#!/usr/bin/env python3
"""e2e quit-clean: quitting the app, in every way, signals nothing outside its own process (#183: quitting logged a
Linux user out of their session, a kill(-pid) with a pid of 0 or a reused group).

Usage: quit_clean.py APP SCRIPT_DIR [OUT_DIR]   (SDL_VIDEO_DRIVER=offscreen; SCULPTFORGE_BRUSH/_COLMAP/_FFMPEG = the fake tools)

The app runs in THIS process group, as it does from a terminal, with a sentinel beside it in the same group (it dies of
any signal sent to the group) and this script watching for SIGTERM / SIGHUP / SIGINT / SIGQUIT / SIGUSR1 / SIGUSR2 sent
to it. Three runs: quit_children.sfui (every way a trainer child ends), then quit_hold.sfui quit by SIGTERM and by SIGINT
(SDL turns both into its quit event, as the window's close button) while a trainer runs. After each: the sentinel lives, nobody signalled this script, no trainer child is left behind.
"""
import os
import signal
import subprocess
import sys
import tempfile
import time

app, script_dir = sys.argv[1], sys.argv[2]
out_root = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp(prefix="sf-e2e-quit-")
received = []
for s in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT, signal.SIGQUIT, signal.SIGUSR1, signal.SIGUSR2):
    signal.signal(s, lambda n, f: received.append(signal.Signals(n).name))

failures = []


def fail(msg):
    failures.append(msg)
    print("FAIL " + msg)


def sentinel():
    return subprocess.Popen([sys.executable, "-c", "import time\nwhile True: time.sleep(1)"])  # same group


TAG = "sf-e2e-quit-%d" % os.getpid()  # in the environment of the app and so of every child it starts


def trainers():
    """Processes of this run (the tag in their environment) still alive, apart from the app itself, by pid."""
    mine = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit() or int(pid) == os.getpid():
            continue
        try:
            with open("/proc/%s/environ" % pid, "rb") as f:
                if TAG.encode() not in f.read():
                    continue
            with open("/proc/%s/stat" % pid) as f:
                if f.read().split(") ")[-1].split()[0] == "Z":
                    continue  # a zombie is gone, only not waited for yet
        except OSError:
            continue
        mine.append(pid)
    return mine


def run(name, script, how=None):
    del received[:]
    guard = sentinel()
    out = os.path.join(out_root, name)
    os.makedirs(out, exist_ok=True)
    p = subprocess.Popen([app, "--ui-test", os.path.join(script_dir, script), "--ui-out", out],
                         env=dict(os.environ, SF_E2E_QUIT_TAG=TAG), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    code = None
    if how is None:
        try:
            code = p.wait(timeout=240)
        except subprocess.TimeoutExpired:
            p.kill()
            fail(name + ": timed out")
    else:
        # Wait until the trainer runs, then quit the app the way asked.
        deadline = time.time() + 90
        while time.time() < deadline and p.poll() is None and len(trainers()) < 2:  # the app and a child
            time.sleep(0.2)
        if len(trainers()) < 2:
            fail(name + ": the trainer never started")
        time.sleep(0.5)
        p.send_signal(how)
        try:
            code = p.wait(timeout=30)
        except subprocess.TimeoutExpired:
            p.kill()
            fail(name + ": the app didn't quit within 30 s of " + signal.Signals(how).name)
    text = p.stdout.read() if p.stdout else ""
    if how is None and code != 0:
        fail(name + ": exit " + str(code) + "\n" + text[-2000:])
    time.sleep(0.5)
    if guard.poll() is not None:
        fail(name + ": the sentinel in our process group was killed (exit " + str(guard.poll()) + ")")
    guard.kill()
    guard.wait()
    if received:
        fail(name + ": this process was signalled: " + ", ".join(received))
    left = trainers()  # the app has exited: anything with the tag now is a child it left behind
    if left:
        fail(name + ": trainer processes left behind: " + " ".join(left))
        for pid in left:
            try:
                os.kill(int(pid), signal.SIGKILL)
            except OSError:
                pass
    print(("ok   " if not failures else "FAIL ") + name)


run("children", "quit_children.sfui")
run("sigterm", "quit_hold.sfui", signal.SIGTERM)
run("sigint", "quit_hold.sfui", signal.SIGINT)
if failures:
    print("\n".join(failures))
    sys.exit(1)
print("PASS quit_clean")
