#!/usr/bin/env python3
"""Writes the update feed of a release, the appcast.xml Oblada's updater reads (Sparkle's
format, see oblada/browser/updater): the version, where its update archive is (the .aar
dmg.sh writes next to the DMG), and the archive's EdDSA signature. The feed itself is
signed too, as the app requires (SURequireSignedFeed).

usage: appcast.py --archive <aar> --url <archive's url> --version <x.y.z> --out <appcast.xml>
                  [--changelog <changelogs/x_y_z.json>] [--key-file <private key>]

Signs with Sparkle's sign_update (sparkle.sh puts it in the checkout), with the key in
the login keychain (make update-key), or with the one in --key-file for tests.
"""
import argparse
import email.utils
import html
import json
import os
import plistlib
import re
import subprocess
import sys
import tempfile

SIGN_UPDATE = os.path.join(
    os.environ.get('OBLADA_CHROMIUM', os.path.expanduser('~/chromium')),
    'src/third_party/sparkle/bin/sign_update')

# The changelog's lists, as the feed shows them.
SECTIONS = [
    ('added', 'Added'),
    ('changed', 'Changed'),
    ('removed', 'Removed'),
    ('fixed', 'Fixed'),
    ('knownIssues', 'Known issues'),
    ('notes', 'Notes'),
]


def sign(path, key_file):
    command = [SIGN_UPDATE, path]
    if key_file:
        command += ['--ed-key-file', key_file]
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def minimum_system_version(archive):
    """The app's LSMinimumSystemVersion, read from the archive."""
    info = 'Oblada.app/Contents/Info.plist'
    with tempfile.TemporaryDirectory() as out:
        subprocess.run(['aa', 'extract', '-i', archive, '-d', out, '-include-path', info],
                       check=True)
        with open(os.path.join(out, info), 'rb') as f:
            return plistlib.load(f)['LSMinimumSystemVersion']


def release_notes(changelog):
    if not changelog:
        return ''
    with open(changelog) as f:
        log = json.load(f)
    parts = []
    for key, title in SECTIONS:
        if log.get(key):
            items = ''.join(f'<li>{html.escape(item)}</li>' for item in log[key])
            parts.append(f'<h3>{title}</h3><ul>{items}</ul>')
    return ''.join(parts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', required=True)
    parser.add_argument('--url', required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--changelog')
    parser.add_argument('--key-file')
    args = parser.parse_args()

    # sparkle:edSignature="…" length="…"
    enclosure = sign(args.archive, args.key_file).strip()
    if not re.fullmatch(r'sparkle:edSignature="[^"]+" length="\d+"', enclosure):
        sys.exit(f'unexpected output from sign_update: {enclosure}')
    version = html.escape(args.version)
    feed = f'''<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Oblada</title>
    <link>https://github.com/Oblada-Browser/oblada/releases</link>
    <item>
      <title>Oblada {version}</title>
      <pubDate>{email.utils.formatdate(usegmt=True)}</pubDate>
      <sparkle:version>{version}</sparkle:version>
      <sparkle:shortVersionString>{version}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>{html.escape(minimum_system_version(args.archive))}</sparkle:minimumSystemVersion>
      <description><![CDATA[{release_notes(args.changelog)}]]></description>
      <enclosure url="{html.escape(args.url)}" type="application/octet-stream" {enclosure}/>
    </item>
  </channel>
</rss>
'''
    with open(args.out, 'w') as f:
        f.write(feed)
    # Embeds the feed's own signature in it.
    sign(args.out, args.key_file)


if __name__ == '__main__':
    main()
