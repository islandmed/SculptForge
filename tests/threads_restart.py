#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# #184: a Threads value saved in Preferences > Performance reaches the shared thread pool at the next start. The pool is
# made by the first brush or stamp work, and the App's own Document makes it before App::init: the saved count has to be
# read before that. The test starts the app (a window run, settings in its own folder) so it writes a hardware profile,
# saves threads = 48 on a profile that says the machine has 48, starts it again and reads the "Threads: pool of N" line.
#
#   python3 tests/threads_restart.py path/to/sculptforge DIR
import json
import os
import subprocess
import sys
import threading
import time

WANT = 48


def start(app, env):
    return subprocess.Popen([app], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)


def wait_for(proc, pattern, seconds):
    """Reads the app's log until a line contains `pattern`; returns that line or None."""
    found = []

    def read():
        for line in proc.stderr:
            if pattern in line:
                found.append(line.strip())
                return

    t = threading.Thread(target=read, daemon=True)
    t.start()
    t.join(seconds)
    return found[0] if found else None


def stop(proc):
    proc.terminate()
    try:
        proc.wait(10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def main():
    app, work = sys.argv[1], os.path.abspath(sys.argv[2])
    data = os.path.join(work, "data")
    os.makedirs(data, exist_ok=True)
    env = dict(os.environ, HOME=work, XDG_DATA_HOME=data, XDG_CONFIG_HOME=os.path.join(work, "config"), SF_ISOLATE="0",
               SF_NO_UPDATE_CHECK="1", SF_STARTUP_SPLASH="0")
    env.pop("SF_THREADS", None)  # (a parallel test run sets it: it would win over nothing, but keep the start plain)
    env.pop("CLAUDECODE", None)
    env.pop("SDL_APP_ID", None)
    env.setdefault("SDL_VIDEO_DRIVER", "offscreen")
    profile = os.path.join(data, "SculptForge", "SculptForge", "hardware_profile.json")

    first = start(app, env)  # a first launch measures the machine and saves its profile
    line = wait_for(first, "Threads: pool of", 120)
    deadline = time.time() + 60
    while not os.path.exists(profile) and time.time() < deadline:
        time.sleep(0.5)
    stop(first)
    if not os.path.exists(profile):
        sys.exit("no hardware profile was saved by the first start (log line: %s)" % line)

    with open(profile, encoding="utf-8") as f:
        p = json.load(f)
    p["hardware"]["cpu_threads"] = WANT
    p.setdefault("overrides", {})["threads"] = WANT
    with open(profile, "w", encoding="utf-8") as f:
        json.dump(p, f)

    second = start(app, env)
    line = wait_for(second, "Threads: pool of", 120)
    stop(second)
    if line is None:
        sys.exit("the second start never printed its thread pool size")
    print(line)
    if "pool of %d " % WANT not in line:
        sys.exit("expected a pool of %d threads after the restart, got: %s" % (WANT, line))


if __name__ == "__main__":
    main()
