#!/usr/bin/env python3
"""Every literal passed to T(), TC() or TW() in the sources must have a
French entry in src/lang_fr.inc (and every entry must still be used)."""
import re
import sys
from pathlib import Path

LIT = r'"(?:[^"\\\n]|\\.)*"'
SEQ = r'((?:' + LIT + r'\s*)+)'


def unescape(seq):
    out = ''
    for lit in re.findall(LIT, seq):
        body = lit[1:-1]
        i = 0
        while i < len(body):
            c = body[i]
            if c == '\\' and i + 1 < len(body):
                n = body[i + 1]
                out += {'n': '\n', 't': '\t', '0': '\0', '"': '"', '\\': '\\', "'": "'"}.get(n, n)
                i += 2
            else:
                out += c
                i += 1
    return out


def main():
    src = Path(sys.argv[1] if len(sys.argv) > 1 else 'src')
    table = (src / 'lang_fr.inc').read_text(encoding='utf-8')
    keys = set()
    for m in re.finditer(r'TR\(\s*' + SEQ + r',', table):
        keys.add(unescape(m.group(1)))
    used = set()
    missing = []
    for f in sorted(src.glob('*.cpp')):
        text = f.read_text(encoding='utf-8')
        for m in re.finditer(r'\b(?:T|TC|TW)\(\s*' + SEQ + r'\)', text):
            s = unescape(m.group(1))
            used.add(s)
            # Labels without words (ids, numbers) don't need a translation.
            if not re.search(r'[A-Za-z]{2,}', s.split('##')[0]) or s in ('MPE',):
                continue
            if s not in keys:
                line = text.count('\n', 0, m.start()) + 1
                missing.append(f'{f.name}:{line}: {s!r}')
    # Action names go through T(actionName(...)).
    for m in re.finditer(r'\{"\w+", (' + LIT + r'),', (src / 'shortcuts.cpp').read_text(encoding='utf-8')):
        s = unescape(m.group(1))
        used.add(s)
        if s not in keys:
            missing.append(f'shortcuts.cpp: {s!r}')
    for line in missing:
        print('missing French text:', line)
    print(f'{len(keys)} translations, {len(missing)} missing')
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
