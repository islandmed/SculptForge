#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Creator referral statement (docs/affiliates.md): per referral code, the membership payments made in one month by
members who joined through that creator's link, and the 15% owed, for the first 12 months of each membership.

website/ref.js adds client_reference_id=<code> to the Stripe checkout links; Stripe keeps it on the checkout
session. Renewals carry no code, so a member's code and start date come from their first coded payment (CSV) or
checkout session (API), and every later payment by the same customer counts until 12 months after that start.
Refunds are taken off; Stripe's fees are not (the share is of what the member paid).

    report.py --month 2026-11 --csv payments.csv   # Dashboard > Payments > Export, with the customer and the
                                                   # client_reference_id (or a "ref" metadata) column
    STRIPE_KEY=rk_... report.py --month 2026-11    # or Stripe's API (a restricted read-only key, from the
                                                   # environment only: never stored in this repository)
    report.py selftest                             # ctest affiliate_report: a fake export

CSV columns are found by name, case-insensitive: date (Created date (UTC) / Created (UTC) / created), Amount,
Amount Refunded, Currency, Status, Customer ID (or Customer Email) and the code (any column with
client_reference_id in its name, or ref / affiliate (metadata)). Amounts in the export are in major units ($5.00).
"""

import argparse
import csv
import datetime as dt
import io
import json
import os
import re
import sys
import urllib.parse
import urllib.request
from decimal import Decimal, ROUND_HALF_UP

SHARE = Decimal("0.15")
MONTHS = 12
CODE = re.compile(r"^[a-z0-9_-]{1,32}$")  # the same rule as website/ref.js


def add_months(t, n):
    y, m = divmod(t.month - 1 + n, 12)
    y, m = t.year + y, m + 1
    days = [31, 29 if y % 4 == 0 and (y % 100 or y % 400 == 0) else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    return t.replace(year=y, month=m, day=min(t.day, days[m - 1]))


def parse_time(s):
    s = s.strip()
    if re.fullmatch(r"\d+", s):
        return dt.datetime.fromtimestamp(int(s), dt.timezone.utc).replace(tzinfo=None)
    for f in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%dT%H:%M:%SZ", "%Y-%m-%dT%H:%M:%S", "%Y-%m-%d"):
        try:
            return dt.datetime.strptime(s, f)
        except ValueError:
            pass
    raise ValueError(f"unknown date format: {s!r}")


def money(s):
    s = (s or "").strip().replace(",", "")
    return Decimal(s) if s else Decimal(0)


def pick(headers, *names, contains=None):
    low = {h.strip().lower(): h for h in headers}
    for n in names:
        if n in low:
            return low[n]
    if contains:
        for h in headers:
            if contains in h.lower():
                return h
    return None


def payments_from_csv(text):
    """[(customer, time, amount, currency, code or None)] for the successful payments of a Stripe export."""
    rows = list(csv.DictReader(io.StringIO(text)))
    if not rows:
        return []
    h = rows[0].keys()
    col = {
        "date": pick(h, "created date (utc)", "created (utc)", "created", "date"),
        "amount": pick(h, "amount", "converted amount"),
        "refunded": pick(h, "amount refunded", "converted amount refunded"),
        "currency": pick(h, "currency", "converted currency"),
        "status": pick(h, "status"),
        "customer": pick(h, "customer id", "customer", "customer email"),
        "code": pick(h, "ref (metadata)", "affiliate (metadata)", "client_reference_id", contains="client_reference_id"),
    }
    for k in ("date", "amount", "customer"):
        if not col[k]:
            sys.exit(f"report.py: the export has no {k} column (columns: {', '.join(h)})")
    out = []
    for r in rows:
        if col["status"] and r[col["status"]].strip().lower() not in ("paid", "succeeded"):
            continue
        cust = r[col["customer"]].strip()
        if not cust:
            continue
        amount = money(r[col["amount"]]) - (money(r[col["refunded"]]) if col["refunded"] else 0)
        code = r[col["code"]].strip().lower() if col["code"] else ""
        out.append((cust, parse_time(r[col["date"]]), amount,
                    (r[col["currency"]].strip().lower() if col["currency"] else "usd"), code or None))
    return out


def stripe_get(key, path, params):
    """Every object of a Stripe list endpoint (read-only GETs, paginated)."""
    items, after = [], None
    while True:
        q = dict(params, limit=100, **({"starting_after": after} if after else {}))
        req = urllib.request.Request(f"https://api.stripe.com/v1/{path}?{urllib.parse.urlencode(q)}",
                                     headers={"Authorization": f"Bearer {key}"})
        with urllib.request.urlopen(req, timeout=60) as r:
            page = json.load(r)
        items += page["data"]
        if not page.get("has_more") or not page["data"]:
            return items
        after = page["data"][-1]["id"]


def payments_from_api(key):
    """The same list from the API: coded checkout sessions, then every paid invoice of those customers."""
    out = []
    for s in stripe_get(key, "checkout/sessions", {"status": "complete"}):
        if s.get("client_reference_id") and s.get("customer"):
            out.append((s["customer"], parse_time(str(s["created"])), Decimal(0), (s.get("currency") or "usd"),
                        s["client_reference_id"].lower()))
    for cust in sorted({p[0] for p in out}):
        for inv in stripe_get(key, "invoices", {"customer": cust, "status": "paid"}):
            paid = (inv.get("status_transitions") or {}).get("paid_at") or inv["created"]
            amount = Decimal(inv.get("amount_paid", 0)) - Decimal(inv.get("post_payment_credit_notes_amount", 0))
            out.append((cust, parse_time(str(paid)), amount / 100, inv.get("currency", "usd"), None))
    return out


def statement(payments, month):
    """{code: {currency: [count, gross]}} for the payments in month (YYYY-MM) inside each membership's first 12
    months; skipped: invalid codes."""
    y, m = map(int, month.split("-"))
    lo = dt.datetime(y, m, 1)
    hi = add_months(lo, 1)
    start = {}  # customer -> (first coded time, code)
    for cust, t, _, _, code in sorted(payments, key=lambda p: p[1]):
        if code and cust not in start:
            if CODE.match(code):
                start[cust] = (t, code)
            else:
                print(f"report.py: ignored invalid code {code!r} ({cust})", file=sys.stderr)
    out = {}
    for cust, t, amount, cur, _ in payments:
        if cust not in start or not (lo <= t < hi) or amount <= 0:
            continue
        t0, code = start[cust]
        if not (t0 <= t < add_months(t0, MONTHS)):
            continue
        c = out.setdefault(code, {}).setdefault(cur, [0, Decimal(0)])
        c[0] += 1
        c[1] += amount
    return out


def render(month, st):
    cents = Decimal("0.01")
    lines = [f"SculptForge creator statement, {month} ({int(SHARE * 100)}% of membership payments, first "
             f"{MONTHS} months of each membership)", ""]
    if not st:
        lines.append("No referred membership payments this month.")
    for code in sorted(st):
        for cur, (n, gross) in sorted(st[code].items()):
            owed = (gross * SHARE).quantize(cents, ROUND_HALF_UP)
            lines.append(f"{code:<16} {n:>4} payments  gross {gross.quantize(cents):>10} {cur.upper()}  "
                         f"owed {owed:>9} {cur.upper()}")
    return "\n".join(lines)


SAMPLE = """id,Created date (UTC),Amount,Amount Refunded,Currency,Status,Customer ID,Customer Email,client_reference_id (metadata)
py_1,2026-11-07 10:00:00,5.00,0.00,usd,Paid,cus_A,a@example.com,asknk
py_2,2026-12-07 10:00:00,5.00,0.00,usd,Paid,cus_A,a@example.com,
py_3,2027-11-07 09:59:59,5.00,0.00,usd,Paid,cus_A,a@example.com,
py_4,2027-12-07 10:00:00,5.00,0.00,usd,Paid,cus_A,a@example.com,
py_5,2026-12-01 00:00:00,48.00,0.00,usd,Paid,cus_B,b@example.com,AskNK
py_6,2026-12-02 00:00:00,5.00,5.00,usd,Paid,cus_C,c@example.com,asknk
py_7,2026-12-03 00:00:00,5.00,0.00,usd,Failed,cus_D,d@example.com,asknk
py_8,2026-12-04 00:00:00,5.00,0.00,usd,Paid,cus_E,e@example.com,
py_9,2026-12-05 00:00:00,150.00,0.00,usd,Paid,cus_F,f@example.com,<script>
py_10,2026-12-06 00:00:00,5.00,0.00,usd,Paid,cus_G,g@example.com,other_creator
"""


def selftest():
    p = payments_from_csv(SAMPLE)
    # December: cus_A renewal, cus_B yearly (code case folded); refunded, failed, uncoded and invalid codes don't count.
    assert statement(p, "2026-12") == {"asknk": {"usd": [2, Decimal("53.00")]},
                                       "other_creator": {"usd": [1, Decimal("5.00")]}}, statement(p, "2026-12")
    assert statement(p, "2026-11") == {"asknk": {"usd": [1, Decimal("5.00")]}}
    assert statement(p, "2027-11") == {"asknk": {"usd": [1, Decimal("5.00")]}}  # month 12, one second before the end
    assert statement(p, "2027-12") == {}  # month 13: no longer referred
    assert "asknk" in render("2026-12", statement(p, "2026-12")) and "7.95 USD" in render("2026-12", statement(p, "2026-12"))
    assert add_months(dt.datetime(2026, 1, 31), 1) == dt.datetime(2026, 2, 28)
    print("affiliate report selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", choices=["selftest"])
    ap.add_argument("--month", help="YYYY-MM (default: last month)")
    ap.add_argument("--csv", help="a Stripe payments export")
    a = ap.parse_args()
    if a.cmd == "selftest":
        return selftest()
    month = a.month or add_months(dt.datetime.utcnow().replace(day=1), -1).strftime("%Y-%m")
    if not re.fullmatch(r"\d{4}-(0[1-9]|1[0-2])", month):
        sys.exit("report.py: --month is YYYY-MM")
    if a.csv:
        with open(a.csv, newline="", encoding="utf-8-sig") as f:
            payments = payments_from_csv(f.read())
    elif os.environ.get("STRIPE_KEY"):
        payments = payments_from_api(os.environ["STRIPE_KEY"])
    else:
        sys.exit("report.py: give --csv EXPORT, or set STRIPE_KEY (a restricted read-only key) in the environment")
    print(render(month, statement(payments, month)))


if __name__ == "__main__":
    main()
