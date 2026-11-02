#!/usr/bin/env python3
"""Discord intake: new #ideas / #help forum threads and #bug-reports messages become GitHub issues.

Run by .github/workflows/discord-intake.yml every 30 minutes. Outbound HTTPS polling only: no ports, no inbound
webhooks. Discord text is untrusted data: it is only ever quoted (as a blockquote, with @ and < neutralised) into
an issue body, never interpreted. Attachments become links; nothing is downloaded.

State lives in the issues themselves: every issue this script opens ends with a marker
`<!-- discord-intake kind=<idea|help|bug> id=<snowflake> channel=<snowflake> -->`, and the run starts by listing
all `from-discord` issues, so a message is never filed twice. When such an issue is closed, its closing comment
(the last one by the owner, a member or a collaborator) goes back to the thread once; the `discord-notified`
label records that.

Environment: DISCORD_BOT_TOKEN (read-only bot: View Channels + Read Message History on the 3 channels),
GITHUB_TOKEN, GITHUB_REPOSITORY, optional DISCORD_GUILD_ID, DISCORD_{IDEAS,HELP,BUGS}_WEBHOOK (replies),
INTAKE_MAX_NEW (10), INTAKE_MAX_AGE_DAYS (30). `--dry-run` reads only and prints what it would do.
`selftest` runs tests/discord_intake/test_discord_intake.py.
"""
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone

DISCORD_API = "https://discord.com/api/v10"
UA = "SculptForge-discord-intake (https://sculptforge.app, 1.0)"
AVATAR = "https://sculptforge.app/img/icon-256.png"
CHANNELS = {"ideas": "idea", "help": "help", "bug-reports": "bug"}  # Discord channel name -> issue kind / label
FORUM_KINDS = ("idea", "help")
LABELS = {"from-discord": "5865f2", "idea": "a2eeef", "help": "d876e3", "bug": "d73a4a", "discord-notified": "c5def5"}
MIN_CHARS = 20
DISCORD_EPOCH_MS = 1420070400000
MARKER_RE = re.compile(r"<!-- discord-intake kind=(idea|help|bug) id=(\d+) channel=(\d+) -->")
CDN_HOSTS = ("cdn.discordapp.com", "media.discordapp.net")
STAFF = ("OWNER", "MEMBER", "COLLABORATOR")
SKIP_TYPES = {24}  # AUTO_MODERATION_ACTION; AutoMod-removed messages are deleted and never listed at all


class HttpError(Exception):
    def __init__(self, status, url, body=""):
        super().__init__(f"HTTP {status} for {url}: {body[:200]}")
        self.status = status


def urllib_transport(method, url, headers, body):
    """The real transport: returns (status, parsed JSON or None). Tests swap in a fake."""
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers=dict(headers, **{"User-Agent": UA}))
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read()
            return r.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            parsed = json.loads(raw)
        except ValueError:
            parsed = {"message": raw}
        return e.code, parsed


class Client:
    def __init__(self, transport, base, headers, sleep=time.sleep):
        self.transport, self.base, self.headers, self.sleep = transport, base, headers, sleep

    def call(self, method, path, body=None, ok404=False):
        url = path if path.startswith("https://") else self.base + path
        for _ in range(3):
            status, data = self.transport(method, url, self.headers, body)
            if status == 429:  # Discord rate limit: wait as told, at most 10 s, then retry
                self.sleep(min(float((data or {}).get("retry_after", 1)), 10))
                continue
            if status == 404 and ok404:
                return None
            if status >= 400:
                raise HttpError(status, url, json.dumps(data))
            return data
        raise HttpError(429, url, "still rate limited")


# ---- formatting (pure) -----------------------------------------------------------------------------------------

def snowflake_time(sid):
    return datetime.fromtimestamp(((int(sid) >> 22) + DISCORD_EPOCH_MS) / 1000, tz=timezone.utc)


def neutralise(text):
    """Untrusted text: no GitHub @mentions, no HTML (so no fake intake markers), no stray control characters."""
    text = re.sub(r"[\x00-\x08\x0b-\x1f\x7f]", "", text.replace("\r\n", "\n"))
    return text.replace("<", "&lt;").replace(">", "&gt;").replace("@", "@\u200b")


def quote(text):
    lines = neutralise(text).strip().split("\n") or [""]
    return "\n".join("> " + line if line.strip() else ">" for line in lines)


def clean_title(text, limit=120):
    text = re.sub(r"\s+", " ", re.sub(r"[\x00-\x1f\x7f]", " ", text)).strip()
    return text if len(text) <= limit else text[: limit - 1].rstrip() + "…"


def attachment_links(attachments):
    """Links only, never downloaded; only Discord CDN https URLs are kept."""
    out = []
    for a in attachments or []:
        url = a.get("url", "")
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != "https" or parsed.hostname not in CDN_HOSTS:
            continue
        name = re.sub(r"[\[\]()<>`*_\\\n]", "", a.get("filename", "file"))[:100] or "file"
        size = a.get("size")
        out.append(f"- [{name}]({url})" + (f" ({size // 1024} KB)" if isinstance(size, int) else ""))
    return out


def display_name(author):
    return clean_title(author.get("global_name") or author.get("username") or "unknown", 80)


def issue_for(kind, item, guild_id):
    """item: {id, channel_id, title, message}. Returns (title, body)."""
    msg = item["message"]
    link = f"https://discord.com/channels/{guild_id}/{item['channel_id']}"
    link += f"/{item['id']}" if kind == "bug" else ""
    where = {"idea": "#ideas", "help": "#help", "bug": "#bug-reports"}[kind]
    date = snowflake_time(msg["id"]).strftime("%Y-%m-%d %H:%M UTC")
    body = [f"**{neutralise(display_name(msg.get('author', {})))}** posted in Discord {where} on {date}: [open on Discord]({link})",
            "", quote(msg.get("content", "")) if msg.get("content", "").strip() else "> (no text)"]
    links = attachment_links(msg.get("attachments"))
    if links:
        body += ["", "Attachments (links only, not downloaded; Discord links expire after a while):"] + links
    body += ["", "_Filed automatically from Discord. The text above is quoted as posted._",
             f"<!-- discord-intake kind={kind} id={item['id']} channel={item['channel_id']} -->"]
    return clean_title(item["title"]) or "Discord post", "\n".join(body)


def bug_title(content):
    first = next((ln for ln in content.split("\n") if ln.strip()), "")
    return "Bug report: " + clean_title(first, 100)


# ---- the run ---------------------------------------------------------------------------------------------------

class Intake:
    def __init__(self, discord, github, repo, webhooks, guild_id=None, max_new=10, max_age_days=30,
                 dry_run=False, now=None, log=print):
        self.d, self.g, self.repo, self.webhooks = discord, github, repo, webhooks
        self.guild_id, self.max_new, self.max_age_days, self.dry = guild_id, max_new, max_age_days, dry_run
        self.now = now or datetime.now(timezone.utc)
        self.log = log
        self.created, self.notified = [], []

    # GitHub
    def known_issues(self):
        known, page = {}, 1
        while True:
            batch = self.g.call("GET", f"/repos/{self.repo}/issues?labels=from-discord&state=all&per_page=100&page={page}")
            for issue in batch or []:
                m = MARKER_RE.search(issue.get("body") or "")
                if m and "pull_request" not in issue:
                    known[m[2]] = (issue, m[1], m[3])
            if not batch or len(batch) < 100:
                return known
            page += 1

    def ensure_labels(self):
        for name, color in LABELS.items():
            if self.g.call("GET", f"/repos/{self.repo}/labels/{name}", ok404=True) is None and not self.dry:
                self.g.call("POST", f"/repos/{self.repo}/labels", {"name": name, "color": color})

    # Discord
    def resolve_guild(self):
        if self.guild_id:
            return self.guild_id
        guilds = self.d.call("GET", "/users/@me/guilds")
        if len(guilds) != 1:
            raise SystemExit(f"the bot is in {len(guilds)} servers; set DISCORD_GUILD_ID")
        return guilds[0]["id"]

    def channels(self):
        found = {}
        for ch in self.d.call("GET", f"/guilds/{self.guild_id}/channels"):
            kind = CHANNELS.get(ch.get("name"))
            if kind and ch.get("type") in ((15,) if kind in FORUM_KINDS else (0, 5)):
                found[kind] = ch["id"]
        return found

    def recent(self, sid):
        return (self.now - snowflake_time(sid)).days < self.max_age_days

    def forum_threads(self, chans):
        forums = {chans[k]: k for k in FORUM_KINDS if k in chans}
        threads = {}
        active = self.d.call("GET", f"/guilds/{self.guild_id}/threads/active") or {}
        for t in active.get("threads", []):
            if t.get("parent_id") in forums:
                threads[t["id"]] = t
        for fid in forums:  # quiet threads auto-archive; still pick up recent ones
            archived = self.d.call("GET", f"/channels/{fid}/threads/archived/public?limit=50", ok404=True) or {}
            for t in archived.get("threads", []):
                threads.setdefault(t["id"], t)
        out = []
        for t in threads.values():
            if self.recent(t["id"]):
                out.append((forums[t["parent_id"]], t))
        return out

    def candidates(self, chans, known):
        """Oldest first: (kind, item) for every new post that passes the filters."""
        items = []
        for kind, t in self.forum_threads(chans):
            if t["id"] in known:
                continue
            msg = self.d.call("GET", f"/channels/{t['id']}/messages/{t['id']}", ok404=True)  # starter message
            if msg is None:
                self.log(f"skip thread {t['id']}: starter message deleted (AutoMod or author)")
                continue
            items.append((kind, {"id": t["id"], "channel_id": t["id"], "title": t.get("name", ""), "message": msg}))
        if "bug" in chans:
            for msg in self.d.call("GET", f"/channels/{chans['bug']}/messages?limit=100") or []:
                if msg["id"] in known or not self.recent(msg["id"]) or msg.get("type", 0) != 0:
                    continue  # replies (type 19) are conversation, not new reports
                items.append(("bug", {"id": msg["id"], "channel_id": chans["bug"],
                                      "title": bug_title(msg.get("content", "")), "message": msg}))
        keep = []
        for kind, item in items:
            msg = item["message"]
            author = msg.get("author", {})
            if msg.get("type", 0) in SKIP_TYPES or author.get("bot") or msg.get("webhook_id"):
                continue
            text = (msg.get("content") or "").strip()
            if len(text) < MIN_CHARS:
                if not text and not msg.get("attachments"):
                    self.log(f"skip {item['id']}: empty text (is the bot's Message Content intent on?)")
                else:
                    self.log(f"skip {item['id']}: shorter than {MIN_CHARS} characters")
                continue
            keep.append((kind, item))
        keep.sort(key=lambda ki: int(ki[1]["id"]))
        return keep

    def post_webhook(self, kind, thread_id, content):
        hook = self.webhooks.get(kind)
        if not hook:
            return False
        if self.dry:
            self.log(f"[dry-run] would post to {kind} webhook: {content[:80]!r}")
            return True
        url = hook + ("&" if "?" in hook else "?") + "wait=true" + (f"&thread_id={thread_id}" if thread_id else "")
        self.d.call("POST", url, {"content": content[:2000], "username": "SculptForge", "avatar_url": AVATAR,
                                  "allowed_mentions": {"parse": []}, "flags": 4})
        return True

    def file_new(self, chans, known):
        cands = self.candidates(chans, known)
        if len(cands) > self.max_new:
            self.log(f"{len(cands)} new posts; filing the oldest {self.max_new}, the rest next run")
        for kind, item in cands[: self.max_new]:
            title, body = issue_for(kind, item, self.guild_id)
            if self.dry:
                self.log(f"[dry-run] would file {kind}: {title!r}")
                continue
            issue = self.g.call("POST", f"/repos/{self.repo}/issues",
                                {"title": title, "body": body, "labels": ["from-discord", kind]})
            self.created.append(issue["number"])
            self.log(f"filed #{issue['number']} ({kind}) from {item['id']}")
            n = issue["number"]
            if kind == "bug":
                text = (f"Thanks! The bug report https://discord.com/channels/{self.guild_id}/{item['channel_id']}/"
                        f"{item['id']} is tracked as #{n} on GitHub, we'll update you there and here: {issue['html_url']}")
                thread = None
            else:
                text = f"Thanks! Tracked as #{n} on GitHub, we'll update you there and here: {issue['html_url']}"
                thread = item["id"]
            try:
                self.post_webhook(kind, thread, text)
            except HttpError as e:
                self.log(f"::warning::could not reply on Discord for #{n}: {e}")

    def close_back(self, known):
        for sid, (issue, kind, channel) in sorted(known.items()):
            names = {lab["name"] for lab in issue.get("labels", [])}
            if issue.get("state") != "closed" or "discord-notified" in names or not self.webhooks.get(kind):
                continue
            n = issue["number"]
            comments = self.g.call("GET", f"/repos/{self.repo}/issues/{n}/comments?per_page=100") or []
            staff = [c for c in comments if c.get("author_association") in STAFF and (c.get("body") or "").strip()]
            text = f"Closed on GitHub: #{n} {issue['html_url']}"
            if staff:
                closing = staff[-1]["body"].strip()
                if len(closing) > 1500:
                    closing = closing[:1500].rstrip() + "…"
                text += "\n" + "\n".join("> " + ln for ln in closing.split("\n"))
            if kind == "bug":
                text = f"Your bug report https://discord.com/channels/{self.guild_id}/{channel}/{sid}\n" + text
            try:
                self.post_webhook(kind, None if kind == "bug" else sid, text)
            except HttpError as e:  # thread gone: label it anyway so we don't retry forever
                self.log(f"::warning::could not post the close of #{n} to Discord: {e}")
            if not self.dry:
                self.g.call("POST", f"/repos/{self.repo}/issues/{n}/labels", {"labels": ["discord-notified"]})
            self.notified.append(n)
            self.log(f"closed #{n} posted back to Discord")

    def run(self):
        self.guild_id = self.resolve_guild()
        chans = self.channels()
        missing = [n for n, k in CHANNELS.items() if k not in chans]
        if missing:
            self.log(f"::warning::bot cannot see channel(s): {', '.join('#' + m for m in missing)}")
        self.ensure_labels()
        known = self.known_issues()
        self.close_back(known)
        self.file_new(chans, known)
        self.log(f"done: {len(self.created)} filed, {len(self.notified)} closes posted back")


def main(argv):
    if argv[1:2] == ["selftest"]:
        test = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "discord_intake",
                            "test_discord_intake.py")
        os.execv(sys.executable, [sys.executable, "-I", test])
    env = os.environ.get
    token = env("DISCORD_BOT_TOKEN", "")
    if not token:
        print("DISCORD_BOT_TOKEN is not set; nothing to do.")
        return 0
    repo, gh_token = env("GITHUB_REPOSITORY", ""), env("GITHUB_TOKEN", "")
    if not repo or not gh_token:
        print("GITHUB_REPOSITORY and GITHUB_TOKEN are required", file=sys.stderr)
        return 2
    discord = Client(urllib_transport, DISCORD_API, {"Authorization": "Bot " + token})
    github = Client(urllib_transport, env("GITHUB_API_URL", "https://api.github.com"),
                    {"Authorization": "Bearer " + gh_token, "Accept": "application/vnd.github+json",
                     "X-GitHub-Api-Version": "2022-11-28"})
    webhooks = {k: env(f"DISCORD_{v}_WEBHOOK", "") for k, v in (("idea", "IDEAS"), ("help", "HELP"), ("bug", "BUGS"))}
    Intake(discord, github, repo, {k: v for k, v in webhooks.items() if v}, guild_id=env("DISCORD_GUILD_ID") or None,
           max_new=int(env("INTAKE_MAX_NEW") or 10), max_age_days=int(env("INTAKE_MAX_AGE_DAYS") or 30),
           dry_run="--dry-run" in argv).run()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
