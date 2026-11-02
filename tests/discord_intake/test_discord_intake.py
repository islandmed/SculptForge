#!/usr/bin/env python3
"""tools/discord_intake.py against recorded fake Discord and GitHub responses (fixtures.json): filtering, dedupe,
rate limit, quoting, link-only attachments, replies, close-back. Also checks the workflow YAML. No network."""
import copy
import json
import os
import pathlib
import sys
import unittest
from datetime import timedelta

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import discord_intake as di

FIX = json.loads((HERE / "fixtures.json").read_text())
G = FIX["guilds"][0]["id"]
HOOKS = {"idea": "https://discord.com/api/webhooks/1/ideas", "help": "https://discord.com/api/webhooks/2/help?x=1"}


class Fake:
    """One transport for both APIs; records every call. Unknown requests fail the test."""

    def __init__(self, fix):
        self.fix, self.calls, self.next_issue, self.rate_limited_once = fix, [], 100, False

    def __call__(self, method, url, headers, body):
        self.calls.append((method, url, body))
        f = self.fix
        d, gh = "https://discord.com/api/v10", "https://api.github.com/repos/o/r"
        if url == f"{d}/users/@me/guilds":
            return 200, f["guilds"]
        if url == f"{d}/guilds/{G}/channels":
            if not self.rate_limited_once:
                self.rate_limited_once = True
                return 429, {"retry_after": 0.5}
            return 200, f["channels"]
        if url == f"{d}/guilds/{G}/threads/active":
            return 200, f["active_threads"]
        if url.startswith(f"{d}/channels/1300000000000000101/threads/archived/public"):
            return 200, f["archived_ideas"]
        if url.startswith(f"{d}/channels/1300000000000000102/threads/archived/public"):
            return 200, f["archived_help"]
        if url.startswith(f"{d}/channels/1300000000000000103/messages?"):
            return 200, f["bug_messages"]
        if url.startswith(f"{d}/channels/") and "/messages/" in url:
            sid = url.rsplit("/", 1)[1]
            return (200, f["starters"][sid]) if sid in f["starters"] else (404, {"message": "Unknown Message"})
        if url.startswith("https://discord.com/api/webhooks/"):
            return 200, {"id": "1"}
        if url.startswith(f"{gh}/labels/"):
            return (200, {}) if url.endswith("/bug") else (404, {})
        if url == f"{gh}/labels" and method == "POST":
            return 201, {}
        if url.startswith(f"{gh}/issues?labels=from-discord"):
            return 200, (f["github_issues"] if url.endswith("page=1") else [])
        if url == f"{gh}/issues" and method == "POST":
            self.next_issue += 1
            return 201, {"number": self.next_issue, "html_url": f"https://github.com/o/r/issues/{self.next_issue}"}
        if url == f"{gh}/issues/81/comments?per_page=100":
            return 200, f["comments_81"]
        if url.endswith("/labels") and method == "POST":
            return 200, []
        raise AssertionError(f"unexpected request {method} {url}")

    def posted(self, prefix):
        return [(u, b) for m, u, b in self.calls if m == "POST" and u.startswith(prefix)]


def make(fix=None, **kw):
    fake = Fake(copy.deepcopy(fix or FIX))
    sleeps = []
    discord = di.Client(fake, di.DISCORD_API, {}, sleep=sleeps.append)
    github = di.Client(fake, "https://api.github.com", {})
    now = di.snowflake_time("1434000000000000013") + timedelta(days=1)
    logs = []
    intake = di.Intake(discord, github, "o/r", kw.pop("webhooks", HOOKS), now=now, log=logs.append, **kw)
    return intake, fake, logs, sleeps


class IntakeTest(unittest.TestCase):
    def test_files_only_new_valid_posts_oldest_first(self):
        intake, fake, logs, sleeps = make()
        intake.run()
        issues = [b for _, b in fake.posted("https://api.github.com/repos/o/r/issues") if b and "title" in b]
        self.assertEqual([i["title"] for i in issues], [
            "Curve brush along a spline please", "Pen pressure not working on Linux",
            "Bug report: Crash when saving a 20M mesh to OBJ"])
        self.assertEqual([i["labels"] for i in issues], [["from-discord", "idea"], ["from-discord", "help"],
                                                          ["from-discord", "bug"]])
        self.assertEqual(sleeps, [0.5])  # 429 honoured once, then retried
        self.assertTrue(any("shorter than 20" in line for line in logs))  # "hi"
        self.assertTrue(any("starter message deleted" in line for line in logs))  # AutoMod-removed thread
        created_labels = [b["name"] for _, b in fake.posted("https://api.github.com/repos/o/r/labels")]
        self.assertEqual(sorted(created_labels), ["discord-notified", "from-discord", "help", "idea"])

    def test_dedupe_second_run_files_nothing(self):
        intake, fake, _, _ = make()
        intake.run()
        fix = copy.deepcopy(FIX)
        for _, body in fake.posted("https://api.github.com/repos/o/r/issues"):
            if body and "title" in body:
                fix["github_issues"].append({"number": 1, "state": "open", "body": body["body"],
                                             "labels": [{"name": n} for n in body["labels"]]})
        intake2, fake2, _, _ = make(fix)
        intake2.run()
        self.assertEqual([b for _, b in fake2.posted("https://api.github.com/repos/o/r/issues") if "title" in b], [])

    def test_rate_limit(self):
        fix = copy.deepcopy(FIX)
        for i in range(30):
            sid = str(1434000000000001000 + i)
            fix["bug_messages"].append({"id": sid, "type": 0, "author": {"id": "4", "username": "spammer"},
                                        "content": f"spam spam spam spam number {i}", "attachments": []})
        intake, _, logs, _ = make(fix)
        intake.run()
        self.assertEqual(len(intake.created), 10)
        self.assertTrue(any("filing the oldest 10" in line for line in logs))
        intake, _, _, _ = make(fix, max_new=3)
        intake.run()
        self.assertEqual(len(intake.created), 3)

    def test_quoting_and_link_only_attachments(self):
        intake, fake, _, _ = make()
        intake.run()
        body = next(b["body"] for _, b in fake.posted("https://api.github.com/repos/o/r/issues")
                    if b and b.get("title", "").startswith("Curve"))
        self.assertIn("**Foirgair Wolf** posted in Discord #ideas on 20", body)
        self.assertIn(f"https://discord.com/channels/{G}/1434000000000000001)", body)
        self.assertIn("> Could we get a curve brush?\n> Like @\u200beveryone wants it &lt;!-- discord-intake", body)
        self.assertNotIn("@everyone", body)
        # the only real marker is ours, at the end; the injected one is escaped
        self.assertEqual([m.groups() for m in di.MARKER_RE.finditer(body)],
                         [("idea", "1434000000000000001", "1434000000000000001")])
        self.assertTrue(body.rstrip().endswith("-->"))
        for line in body.split("\n"):
            if "curve brush" in line or "evil.example/p.png" in line:
                self.assertTrue(line.startswith(">"), line)  # message text is only ever in the blockquote
        self.assertIn("- [mockup.png](https://cdn.discordapp.com/attachments/1/2/mockup.png?ex=abc) (200 KB)", body)
        self.assertNotIn("payload.exe", body)  # non-Discord-CDN links are dropped
        downloads = [u for m, u, _ in fake.calls if "cdn.discordapp.com" in u or "evil.example" in u]
        self.assertEqual(downloads, [])  # attachments are never fetched

    def test_replies_and_close_back(self):
        intake, fake, _, _ = make()
        intake.run()
        hooks = fake.posted("https://discord.com/api/webhooks/")
        urls = [u for u, _ in hooks]
        # issue #81 (help, closed) posted once, quoting the owner's last comment, not drive-by ones
        close = next(b for u, b in hooks if "thread_id=1433000000000000007" in u)
        self.assertIn("Closed on GitHub: #81", close["content"])
        self.assertIn("> Fixed in 0.10.8", close["content"])
        self.assertNotIn("+1", close["content"])
        self.assertEqual(close["allowed_mentions"], {"parse": []})
        self.assertEqual(close["username"], "SculptForge")
        self.assertIn("https://discord.com/api/webhooks/2/help?x=1&wait=true&thread_id=1433000000000000007", urls)
        self.assertNotIn("1433000000000000008", " ".join(urls))  # #82 already notified
        labelled = [u for u, b in fake.posted("https://api.github.com/repos/o/r/issues/") if u.endswith("/labels")]
        self.assertEqual(labelled, ["https://api.github.com/repos/o/r/issues/81/labels"])
        thanks = next(b for u, b in hooks if "thread_id=1434000000000000001" in u)
        self.assertTrue(thanks["content"].startswith("Thanks! Tracked as #101 on GitHub, we'll update you there and here"))
        self.assertEqual(len(hooks), 3)  # no bug webhook configured: the bug report gets no reply

    def test_dry_run_writes_nothing(self):
        intake, fake, logs, _ = make(dry_run=True)
        intake.run()
        self.assertEqual([c for c in fake.calls if c[0] != "GET"], [])
        self.assertEqual(sum("[dry-run] would file" in line for line in logs), 3)

    def test_no_token_skips_quietly(self):
        env = dict(os.environ)
        os.environ.pop("DISCORD_BOT_TOKEN", None)
        try:
            self.assertEqual(di.main(["discord_intake.py"]), 0)
        finally:
            os.environ.clear()
            os.environ.update(env)


class WorkflowTest(unittest.TestCase):
    PATH = ROOT / ".github" / "workflows" / "discord-intake.yml"

    def test_workflow(self):
        text = self.PATH.read_text()
        self.assertNotIn("\t", text)
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML not installed; structural checks skipped")
        wf = yaml.safe_load(text)
        on = wf.get("on", wf.get(True))
        self.assertEqual(on["schedule"], [{"cron": "*/30 * * * *"}])
        self.assertIn("workflow_dispatch", on)
        self.assertEqual(wf["permissions"], {"issues": "write", "contents": "read"})
        steps = wf["jobs"]["intake"]["steps"]
        run = " ".join(s.get("run", "") for s in steps)
        self.assertIn("tools/discord_intake.py", run)
        envs = {k: v for s in steps for k, v in (s.get("env") or {}).items()}
        self.assertEqual(envs["DISCORD_BOT_TOKEN"], "${{ secrets.DISCORD_BOT_TOKEN }}")


if __name__ == "__main__":
    unittest.main(verbosity=1)
