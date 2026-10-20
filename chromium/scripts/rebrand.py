#!/usr/bin/env python3
"""Renames Chromium to Oblada in the browser's strings, in every language.

Translations (.xtb) are looked up by a fingerprint of the English text, so changing the
English text alone would silently drop every translation. This rewrites the English text,
recomputes each changed message's fingerprint with grit, and moves the translations to the
new fingerprints with the same renaming applied.

Messages that talk about the Chromium project itself ("...the Chromium open source
project...") are left alone. Re-running is harmless. Prints the files it changed.

usage: rebrand.py <chromium src dir>
"""
import pathlib
import re
import sys

SRC = pathlib.Path(sys.argv[1]).resolve()
sys.path.insert(0, str(SRC / 'tools' / 'grit'))
from grit import grd_reader  # noqa: E402
from grit.node import message as message_node  # noqa: E402

# (grd, folder of its .xtb files)
STRING_FILES = [
    ('chrome/app/chromium_strings.grd', 'chrome/app/resources'),
    ('components/components_chromium_strings.grd', 'components/strings'),
]
NAME = re.compile(r'Chromium(?![ ]?OS)(?!\w)')
PROTECTED = re.compile(r'Chromium (open[- ]source|Authors|project)', re.I)
MESSAGE = re.compile(r'(<message\b[^>]*>)(.*?)(</message>)', re.S)
TRANSLATION = re.compile(r'<translation id="(\d+)">(.*?)</translation>', re.S)


def message_ids(grd):
    """Fingerprints in document order (names repeat across <if> branches, so they can't be keys)."""
    root = grd_reader.Parse(str(grd), skip_validation_checks=True)
    return [node.GetCliques()[0].GetMessage().GetId()
            for node in root.Preorder() if isinstance(node, message_node.MessageNode)]


def rename_messages(path):
    text = path.read_text(encoding='utf-8')

    def rename(m):
        body = m.group(2)
        if PROTECTED.search(body):
            return m.group(0)
        return m.group(1) + NAME.sub('Oblada', body) + m.group(3)

    renamed = MESSAGE.sub(rename, text)
    if renamed != text:
        path.write_text(renamed, encoding='utf-8')
        return True
    return False


changed = []
for grd_name, xtb_dir in STRING_FILES:
    grd = SRC / grd_name
    before = message_ids(grd)
    parts = [grd] + [grd.parent / p for p in re.findall(r'<part file="([^"]+)"', grd.read_text(encoding='utf-8'))]
    changed += [p for p in parts if rename_messages(p)]
    after = message_ids(grd)
    moved = {old: new for old, new in zip(before, after) if old != new}
    if not moved:
        continue
    stem = grd.stem
    for xtb in sorted((SRC / xtb_dir).glob(f'{stem}_*.xtb')):
        text = xtb.read_text(encoding='utf-8')

        def move(m):
            new_id = moved.get(m.group(1))
            if not new_id:
                return m.group(0)
            return f'<translation id="{new_id}">{NAME.sub("Oblada", m.group(2))}</translation>'

        updated = TRANSLATION.sub(move, text)
        if updated != text:
            xtb.write_text(updated, encoding='utf-8')
            changed.append(xtb)

for path in changed:
    print(path.relative_to(SRC))
