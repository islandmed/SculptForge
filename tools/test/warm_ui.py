#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Warm UI tests: a pool of `sculptforge --ui-serve` servers (src/app/UiServe.h) runs tests/ui/*.sfui scripts
without starting a process, SDL, a window and an OpenGL context for each one.

  warm_ui.py start  --dir D --app APP      start the pool in the background (ctest fixture ui_warm)
  warm_ui.py client --dir D --app APP -- ARGS...   run one test through the pool, as `APP ARGS...` would run it
                                           cold: same output, same exit code (each ctest ui_* test)
  warm_ui.py stop   --dir D                stop the pool (ctest fixture cleanup)
  warm_ui.py suite  --build B [--cold] [--repeat K] [--shuffle] [--json F]
                                           every ui test ctest knows, through a pool of its own (or cold), with
                                           wall time and screenshot hashes: to compare warm with cold
  warm_ui.py compare COLD.json WARM.json   verdicts and screenshots of WARM's runs that differ from what every run
                                           in COLD agrees on

The pool talks to its servers over their standard input and output, and clients reach the pool through files in
D (made private to this user, 0700): no port, no socket. A server that crashes or hangs is restarted and its script
rerun cold, once. A script with a `# cold` line, or SF_UI_COLD=1 in the environment, always runs cold. Servers:
SF_UI_SERVERS or half the CPU threads.
"""
import argparse
import hashlib
import json
import os
import queue
import random
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import uuid

# Seconds a script may take: ctest gives a ui test 300; a test with its own ctest TIMEOUT sets SF_UI_TIMEOUT a little
# under it (ui_mode_matrix: 1200, 3-10 min on a loaded CI runner).
TIMEOUT = int(os.environ.get("SF_UI_TIMEOUT", "290"))
IDLE_EXIT = 900  # a pool nobody used for this long stops (a ctest run that never reached the cleanup)


def servers_wanted():
    n = os.environ.get("SF_UI_SERVERS")
    return max(1, int(n)) if n else max(1, (os.cpu_count() or 2) // 2)


def script_of(args):
    return args[args.index("--ui-test") + 1] if "--ui-test" in args[:-1] else None


def wants_cold(args):
    """Runs that stay cold: anything but a UI test script, scripts marked `# cold`, and SF_UI_COLD=1."""
    if os.environ.get("SF_UI_COLD", "0") not in ("", "0"):
        return True
    script = script_of(args)
    if not script:
        return True
    try:
        with open(script, encoding="utf-8") as f:
            return any(re.match(r"\s*#\s*cold\b", line) for line in f)
    except OSError:
        return True


def run_cold(app, args, env, cwd, timeout=TIMEOUT):
    """(exit code, output) of one cold run, as ctest would see it."""
    try:
        p = subprocess.run([app] + args, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           stdin=subprocess.DEVNULL, timeout=timeout)
        return p.returncode, p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        return 124, (e.stdout or b"").decode("utf-8", "replace") + f"\nwarm_ui: cold run timed out after {timeout} s\n"


class Server:
    """One `APP --ui-serve`; run() sends it a script and waits for the answer."""

    def __init__(self, app, logdir, index, threads):
        self.app, self.index, self.threads = app, index, threads
        self.errlog = os.path.join(logdir, f"server-{index}.log")
        self.proc = None
        self.lines = None
        self.runs = 0

    def start(self):
        self.lines = queue.Queue()
        err = open(self.errlog, "ab")
        self.proc = subprocess.Popen([self.app, "--ui-serve", "--threads", str(self.threads)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=err, bufsize=0)
        err.close()
        self.runs = 0
        out, lines = self.proc.stdout, self.lines

        def read():
            for raw in iter(out.readline, b""):
                lines.put(raw.decode("utf-8", "replace").rstrip("\r\n"))
            lines.put(None)  # the server is gone

        threading.Thread(target=read, daemon=True).start()
        if self._answer(120) == "ready":
            return True
        self.stop(kill=True)
        return False

    def start_retrying(self, tries=4):
        """start(), again after a pause when it fails: an OpenGL context can fail to come up while many others start
        (WSL's d3d12: "Could not find a valid EGL device")."""
        for i in range(tries):
            if self.start():
                return True
            time.sleep(0.5 + random.random() * (i + 1))
        return False

    def _answer(self, timeout):
        try:
            return self.lines.get(timeout=timeout)
        except queue.Empty:
            return None

    def stop(self, kill=False):
        if not self.proc:
            return
        try:
            if kill:
                self.proc.kill()
            else:
                self.proc.stdin.write(b"quit\n")
                self.proc.stdin.flush()
            self.proc.wait(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            self.proc.kill()
        self.proc = None

    def run(self, args, env_changes, cwd, log, timeout):
        """Exit code, or None when the server crashed, hung or lost its GPU device during a failed script (it is
        then gone)."""
        if not self.proc and not self.start_retrying():
            return None
        fields = ["run", log]
        for k, v in env_changes.items():
            fields += ["-e", f"{k}={v}"]
        fields += ["-C", cwd] + args
        if any("\t" in f or "\n" in f for f in fields):
            return None
        try:
            self.proc.stdin.write(("\t".join(fields) + "\n").encode("utf-8"))
            self.proc.stdin.flush()
        except OSError:
            self.stop(kill=True)
            return None
        answer = self._answer(timeout)
        if not answer or not answer.startswith("done\t"):
            self.stop(kill=True)
            return None
        self.runs += 1
        fields = answer.split("\t")
        code = int(fields[1])
        if "lost" in fields[3:]:  # the GPU dropped its device: the server quit; a failure may be that, not the script
            self.stop(kill=True)
            return None if code != 0 else code
        return code


class Pool:
    """N servers; run() takes a free one (blocking) and falls back to a cold run when a server fails."""

    def __init__(self, app, workdir, n):
        self.app, self.workdir = app, workdir
        os.makedirs(os.path.join(workdir, "logs"), exist_ok=True)
        self.free = queue.Queue()
        # The CPU threads shared out: each server's brush thread pool gets its part (at least 2).
        threads = max(2, (os.cpu_count() or 2) // n)
        self.servers = [Server(app, workdir, i, threads) for i in range(n)]
        for s in self.servers:
            self.free.put(s)
        self.base_env = dict(os.environ)
        self.cold_reruns = 0

    def warm_up(self):
        threads = [threading.Thread(target=s.start_retrying) for s in self.servers]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

    def env_changes(self, env):
        return {k: v for k, v in env.items() if self.base_env.get(k) != v}

    def run(self, args, env, cwd, timeout=TIMEOUT):
        """(exit code, output, how): how is warm, cold (asked for) or rerun (a server crashed or hung)."""
        if wants_cold(args) or env.get("SF_UI_COLD", "0") not in ("", "0"):
            return run_cold(self.app, args, env, cwd, timeout) + ("cold",)
        server = self.free.get()
        log = os.path.join(self.workdir, "logs", uuid.uuid4().hex + ".log")
        try:
            code = server.run(args, self.env_changes(env), cwd, log, timeout)
        finally:
            self.free.put(server)
        output = ""
        try:
            with open(log, encoding="utf-8", errors="replace") as f:
                output = f.read()
            os.remove(log)
        except OSError:
            pass
        if code is not None:
            return code, output, "warm"
        self.cold_reruns += 1
        code, cold = run_cold(self.app, args, env, cwd, timeout)
        note = f"warm_ui: the warm server failed on {script_of(args)} (crash, timeout or lost GPU device); rerun cold:\n"
        return code, output + "\n" + note + cold, "rerun"

    def stop(self):
        for s in self.servers:
            s.stop()


# --- the pool as a background process, for ctest -------------------------------------------------------------------

def private_dir(d):
    os.makedirs(d, mode=0o700, exist_ok=True)
    if os.name != "nt":
        os.chmod(d, 0o700)
    for sub in ("queue", "done"):
        os.makedirs(os.path.join(d, sub), mode=0o700, exist_ok=True)


def pid_alive(pid):
    if os.name == "nt":  # (os.kill would end it)
        import ctypes
        h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return False
        code = ctypes.c_ulong()
        ctypes.windll.kernel32.GetExitCodeProcess(h, ctypes.byref(code))
        ctypes.windll.kernel32.CloseHandle(h)
        return code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def pool_alive(d):
    try:
        with open(os.path.join(d, "pool.pid")) as f:
            return pid_alive(int(f.read()))
    except (OSError, ValueError):
        return False


def cmd_start(a):
    if os.environ.get("SF_UI_COLD", "0") not in ("", "0"):
        print("SF_UI_COLD=1: UI tests run cold")
        return 0
    d = os.path.abspath(a.dir)
    cmd_stop(a, quiet=True)
    shutil.rmtree(d, ignore_errors=True)
    private_dir(d)
    log = open(os.path.join(d, "pool.log"), "ab")
    kw = {"start_new_session": True} if os.name != "nt" else {"creationflags": 0x00000008}  # DETACHED_PROCESS
    subprocess.Popen([sys.executable, os.path.abspath(__file__), "serve", "--dir", d, "--app", a.app],
                     stdin=subprocess.DEVNULL, stdout=log, stderr=log, close_fds=True, **kw)
    for _ in range(100):  # until its pid file is there (the servers warm up while the first tests queue)
        if pool_alive(d):
            print(f"warm UI pool started: {servers_wanted()} servers ({d})")
            return 0
        time.sleep(0.05)
    print("warm UI pool did not start; UI tests run cold")
    return 0


def cmd_serve(a):
    d = a.dir
    pool = Pool(a.app, d, servers_wanted())
    with open(os.path.join(d, "pool.pid.tmp"), "w") as f:
        f.write(str(os.getpid()))
    os.replace(os.path.join(d, "pool.pid.tmp"), os.path.join(d, "pool.pid"))
    threading.Thread(target=pool.warm_up, daemon=True).start()
    jobs = queue.Queue()
    taken = set()
    stop = threading.Event()

    def work():
        while not stop.is_set():
            try:
                name = jobs.get(timeout=0.2)
            except queue.Empty:
                continue
            path = os.path.join(d, "queue", name)
            try:
                with open(path, encoding="utf-8") as f:
                    req = json.load(f)
                os.remove(path)
                code, output, how = pool.run(req["args"], req["env"], req["cwd"], req.get("timeout", TIMEOUT))
            except Exception as e:  # a broken request: answer it rather than leave its client waiting
                code, output, how = 1, f"warm_ui: {e}\n", "error"
            tmp = os.path.join(d, "done", name + ".tmp")
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump({"code": code, "output": output, "how": how}, f)
            os.replace(tmp, os.path.join(d, "done", name))

    workers = [threading.Thread(target=work, daemon=True) for _ in pool.servers]
    for w in workers:
        w.start()
    last = time.time()
    while not os.path.exists(os.path.join(d, "stop")) and time.time() - last < IDLE_EXIT:
        names = sorted(n for n in os.listdir(os.path.join(d, "queue")) if n.endswith(".json") and n not in taken)
        for n in names:
            taken.add(n)
            jobs.put(n)
            last = time.time()
        time.sleep(0.01)
    stop.set()
    for w in workers:
        w.join(timeout=TIMEOUT)
    pool.stop()
    try:
        os.remove(os.path.join(d, "pool.pid"))
    except OSError:
        pass
    print(f"warm UI pool stopped; {pool.cold_reruns} cold reruns")
    return 0


def cmd_client(a):
    args = a.args[1:] if a.args[:1] == ["--"] else a.args
    d = os.path.abspath(a.dir)
    if wants_cold(args) or not pool_alive(d):
        code, output = run_cold(a.app, args, dict(os.environ), os.getcwd())
        sys.stdout.write(output)
        return code
    name = f"{time.time():.6f}-{os.getpid()}-{uuid.uuid4().hex[:6]}.json"
    tmp = os.path.join(d, "queue", name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"args": args, "env": dict(os.environ), "cwd": os.getcwd(), "timeout": TIMEOUT}, f)
    os.replace(tmp, os.path.join(d, "queue", name))
    done = os.path.join(d, "done", name)
    checked = time.time()
    while not os.path.exists(done):
        time.sleep(0.02)
        if time.time() - checked > 5:  # the pool died with our request: run it ourselves
            checked = time.time()
            if not pool_alive(d) and not os.path.exists(done):
                code, output = run_cold(a.app, args, dict(os.environ), os.getcwd())
                sys.stdout.write("warm_ui: the pool stopped; ran cold\n" + output)
                return code
    with open(done, encoding="utf-8") as f:
        res = json.load(f)
    os.remove(done)
    sys.stdout.write(res["output"])
    if res["how"] == "rerun":
        sys.stdout.write("warm_ui: [rerun cold]\n")
    return res["code"]


def cmd_stop(a, quiet=False):
    d = os.path.abspath(a.dir)
    if not os.path.isdir(d):
        return 0
    open(os.path.join(d, "stop"), "w").close()
    for _ in range(600):
        if not pool_alive(d):
            break
        time.sleep(0.05)
    if not quiet:
        try:
            with open(os.path.join(d, "pool.log"), encoding="utf-8", errors="replace") as f:
                sys.stdout.write(f.read())
        except OSError:
            pass
    return 0


# --- the whole UI suite, for comparing warm with cold ---------------------------------------------------------------

def ui_tests(build, label_regex):
    out = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1", "-L", label_regex],
                         capture_output=True, text=True, check=True).stdout
    tests = []
    for t in json.loads(out)["tests"]:
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        if props.get("DISABLED") or "command" not in t:
            continue
        cmd = t["command"]
        if "client" in cmd and "--" in cmd:  # a warm client: the app and its arguments
            cmd = [cmd[cmd.index("--app") + 1]] + cmd[cmd.index("--") + 1:]
        env = dict(os.environ)
        for kv in props.get("ENVIRONMENT", []):
            k, _, v = kv.partition("=")
            env[k] = v
        tests.append({"name": t["name"], "app": cmd[0], "args": cmd[1:], "env": env,
                      "cwd": props.get("WORKING_DIRECTORY", build)})
    return tests


def shot_hashes(folder):
    hashes = {}
    for root, _, files in os.walk(folder):
        for n in files:
            if n.endswith(".png"):
                p = os.path.join(root, n)
                with open(p, "rb") as f:
                    hashes[os.path.relpath(p, folder)] = hashlib.sha256(f.read()).hexdigest()[:16]
    return hashes


def cmd_suite(a):
    build = os.path.abspath(a.build)
    tests = [t for t in ui_tests(build, a.label) if script_of(t["args"])]
    if a.regex:
        tests = [t for t in tests if re.search(a.regex, t["name"])]
    shots = os.path.abspath(a.shots or os.path.join(build, "ui-shots-" + ("cold" if a.cold else "warm")))
    for t in tests:  # each run's screenshots in a folder of its own
        i = t["args"].index("--ui-out") + 1 if "--ui-out" in t["args"] else None
        if i:
            t["args"][i] = os.path.join(shots, os.path.basename(t["args"][i]))
    n = a.servers or servers_wanted()
    pool = None
    if not a.cold:
        pool = Pool(tests[0]["app"], os.path.join(build, "ui-warm-suite"), n)
        t0 = time.time()
        pool.warm_up()
        print(f"{n} servers ready in {time.time() - t0:.1f} s")
    results = {}
    for rep in range(a.repeat):
        order = list(tests)
        if a.shuffle:
            random.Random(a.seed + rep).shuffle(order)
        shutil.rmtree(shots, ignore_errors=True)
        work = queue.Queue()
        for t in order:
            work.put(t)
        lock = threading.Lock()
        rep_results = {}

        def go():
            while True:
                try:
                    t = work.get_nowait()
                except queue.Empty:
                    return
                s = time.time()
                if pool:
                    code, out, how = pool.run(t["args"], t["env"], t["cwd"])
                else:
                    code, out = run_cold(t["app"], t["args"], t["env"], t["cwd"])
                    how = "cold"
                with lock:
                    rep_results[t["name"]] = {"code": code, "seconds": round(time.time() - s, 2), "how": how,
                                              "fail": [l for l in out.splitlines() if l.startswith("FAIL")][:5]}
                    if code != 0:
                        rep_results[t["name"]]["tail"] = out.splitlines()[-12:]
                        if a.verbose:
                            print(f"--- {t['name']} ({how}) exit {code}:\n" + "\n".join(out.splitlines()[-12:]))

        t0 = time.time()
        threads = [threading.Thread(target=go) for _ in range(a.jobs or n)]
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        wall = time.time() - t0
        failed = sorted(k for k, r in rep_results.items() if r["code"] != 0)
        print(f"run {rep + 1}: {len(order)} tests in {wall:.1f} s, {len(failed)} failed"
              + (f": {' '.join(failed)}" if failed else ""))
        results[f"run{rep + 1}"] = {"wall": round(wall, 1), "tests": rep_results, "shots": shot_hashes(shots),
                                    "failed": failed}
    if pool:
        print(f"cold reruns: {pool.cold_reruns}")
        pool.stop()
    if a.json:
        with open(a.json, "w") as f:
            json.dump(results, f, indent=1, sort_keys=True)
    return 0 if all(not r["failed"] for r in results.values()) else 1


def cmd_compare(a):
    """The first file's runs are the reference (cold ones, say): a verdict or screenshot counts only where they
    all agree, since some screenshots differ from run to run anyway (GPU timing). Each run of the other files is
    checked against that."""
    def runs_of(path):
        with open(path) as f:
            return [(f"{os.path.basename(path)}:{k}", r) for k, r in sorted(json.load(f).items())]
    ref = runs_of(a.files[0])
    others = [r for path in a.files[1:] for r in runs_of(path)] or ref[1:]
    ref_runs = [r for _, r in ref]
    ok = lambda r, t: r["tests"].get(t, {}).get("code") == 0
    tests = set().union(*(r["tests"] for _, r in ref))
    stable_verdict = {t: ok(ref_runs[0], t) for t in tests if len({ok(r, t) for r in ref_runs}) == 1}
    shots = set().union(*(r["shots"] for r in ref_runs))
    stable_shot = {s: ref_runs[0]["shots"].get(s) for s in shots if len({r["shots"].get(s) for r in ref_runs}) == 1}
    print(f"reference {a.files[0]}: {len(ref)} runs ({', '.join(str(r['wall']) + ' s' for r in ref_runs)}); "
          f"{len(stable_verdict)} of {len(tests)} verdicts and {len(stable_shot)} of {len(shots)} screenshots the same in each")
    differ = 0
    for name, r in others:
        verdicts = sorted(t for t, v in stable_verdict.items() if ok(r, t) != v)
        diff_shots = sorted(s for s, h in stable_shot.items() if r["shots"].get(s) != h)
        differ += len(verdicts) + len(diff_shots)
        print(f"{name} ({r['wall']} s): {len(verdicts)} verdicts and {len(diff_shots)} stable screenshots differ")
        for t in verdicts:
            print(f"  verdict {t}: {r['tests'].get(t, {}).get('code')} {r['tests'].get(t, {}).get('fail')}")
        for s in diff_shots:
            print(f"  shot {s}")
    return 1 if differ else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("start", "serve", "client", "stop"):
        s = sub.add_parser(name)
        s.add_argument("--dir", required=True)
        if name != "stop":
            s.add_argument("--app", required=name != "stop")
        if name == "client":
            s.add_argument("args", nargs=argparse.REMAINDER)
    s = sub.add_parser("suite")
    s.add_argument("--build", required=True)
    s.add_argument("--label", default="^ui$")
    s.add_argument("--regex", help="only the tests whose name matches")
    s.add_argument("--cold", action="store_true", help="every test in a process of its own")
    s.add_argument("--servers", type=int, help="warm servers (default SF_UI_SERVERS or half the CPU threads)")
    s.add_argument("--jobs", type=int, help="tests at once (default: as many as servers)")
    s.add_argument("--repeat", type=int, default=1)
    s.add_argument("--shuffle", action="store_true")
    s.add_argument("--seed", type=int, default=1)
    s.add_argument("--shots", help="screenshot folder (default BUILD/ui-shots-warm or -cold)")
    s.add_argument("--json", help="write every run's results and screenshot hashes here")
    s.add_argument("--verbose", action="store_true", help="print the end of each failed test's output")
    s = sub.add_parser("compare")
    s.add_argument("files", nargs="+")
    a = p.parse_args()
    if os.name != "nt":
        signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    return {"start": cmd_start, "serve": cmd_serve, "client": cmd_client, "stop": cmd_stop,
            "suite": cmd_suite, "compare": cmd_compare}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
