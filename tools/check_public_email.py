#!/usr/bin/env python3
"""Fail if a public file carries our email address (Tony 11-06: hello@ is a private support inbox, spam).

Scans website/, docs/ and README* for a literal "@sculptforge.app" or a mailto: to our domain. The Studios page
builds its address in JavaScript on click, so it is never literal and passes.
  python3 -I tools/check_public_email.py [root]    (test public_email; sitegate.sh greps website/ the same way)
  python3 -I tools/check_public_email.py --self-test
"""
import os, re, sys, tempfile

BAD = re.compile(r'@sculptforge\.app|mailto:[^"\'\s<>]*sculptforge\.app', re.I)
TEXT = ('.html', '.md', '.js', '.json', '.txt', '.xml', '.css', '.svg', '.yml', '.yaml', '.rst', '')


def files(root):
    for top in ('website', 'docs'):
        for d, _, fs in os.walk(os.path.join(root, top)):
            for f in fs:
                yield os.path.join(d, f)
    for f in os.listdir(root):
        if f.startswith('README'):
            yield os.path.join(root, f)


def scan(root):
    hits = []
    for p in files(root):
        if os.path.splitext(p)[1].lower() not in TEXT or not os.path.isfile(p):
            continue
        with open(p, encoding='utf-8', errors='replace') as fh:
            for n, line in enumerate(fh, 1):
                for m in BAD.finditer(line):
                    hits.append(f'{os.path.relpath(p, root)}:{n}: {line[max(0, m.start() - 30):m.end() + 10].strip()}')
    return hits


def self_test():
    cases = {'website/a.html': ('<a href="mailto:hello@sculptforge.app">', True),
             'README.md': ('write to hello@sculptforge.app', True),
             'docs/b.md': ('<a href="MAILTO:Sales@SculptForge.App">', True),
             'website/c.html': ('<a href="mailto:someone@example.org">', False),
             'website/d.js': ("'mailto:' + 'hello' + '@' + 'sculptforge.app'", False),
             'src/e.cpp': ('hello@sculptforge.app', False)}
    with tempfile.TemporaryDirectory() as root:
        for rel, (text, _) in cases.items():
            os.makedirs(os.path.join(root, os.path.dirname(rel)), exist_ok=True)
            with open(os.path.join(root, rel), 'w', encoding='utf-8') as fh:
                fh.write(text + '\n')
        got = {h.split(':', 1)[0] for h in scan(root)}
    want = {rel for rel, (_, bad) in cases.items() if bad}
    if got != want:
        print(f'check_public_email self-test: flagged {sorted(got)}, expected {sorted(want)}')
        return 1
    print('check_public_email self-test: ok')
    return 0


def main():
    if sys.argv[1:] == ['--self-test']:
        return self_test()
    hits = scan(sys.argv[1] if len(sys.argv) > 1 else '.')
    if hits:
        print('check_public_email: our address must not be public (use Discord, or the click-built link on studios.html):')
        print(*('  ' + h for h in hits[:30]), sep='\n')
        return 1
    print('check_public_email: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
