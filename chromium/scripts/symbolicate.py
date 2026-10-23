#!/usr/bin/env python3
"""Names Oblada's functions in a crash report of a release.

Releases are stripped, so macOS's crash reports (.ips, in ~/Library/Logs/DiagnosticReports)
don't name Oblada's functions. release.sh keeps each release's symbols on this Mac, as
dSYMs in $OBLADA_CHROMIUM/symbols/Oblada-<version>-<arch>-dSYMs.aar: this finds the
report's version (its build_version, Oblada's version), unpacks them and prints the
crashed thread with names from atos, the system's frames as the report names them.
  symbolicate.py <report.ips> [--all] [--symbols <dSYMs.aar>]
--all prints every thread; --symbols takes another archive of dSYMs.
"""

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

SYMBOLS = pathlib.Path(os.environ.get('OBLADA_CHROMIUM', pathlib.Path.home() / 'chromium')) / 'symbols'


def uuid_of(path):
    out = subprocess.run(['xcrun', 'dwarfdump', '--uuid', path], capture_output=True, text=True).stdout
    return out.split()[1].lower() if out.startswith('UUID:') else None


def unpack(archive):
    """The archive's dSYMs, unpacked once into the temporary directory, by UUID."""
    dest = pathlib.Path(tempfile.gettempdir()) / f'oblada-{archive.stem}'
    if not dest.is_dir():
        work = pathlib.Path(tempfile.mkdtemp())
        subprocess.run(['aa', 'extract', '-i', archive, '-d', work], check=True)
        work.rename(dest)
    return {uuid_of(dsym): dsym for dsym in dest.glob('*.dSYM')}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('--all', action='store_true', help='every thread, not only the crashed one')
    parser.add_argument('--symbols', type=pathlib.Path, help='the archive of dSYMs to use')
    args = parser.parse_args()

    header, body = args.report.read_text().split('\n', 1)
    header, body = json.loads(header), json.loads(body)
    version = header.get('build_version', '?')
    archive = args.symbols or next(iter(sorted(SYMBOLS.glob(f'Oblada-{version}-*-dSYMs.aar'))), None)
    if not archive:
        sys.exit(f'no symbols for Oblada {version} in {SYMBOLS}: was it released from this Mac?')
    dsyms = unpack(archive)

    images = body['usedImages']
    threads = list(enumerate(body['threads']))
    if not args.all:
        threads = [(i, t) for i, t in threads if t.get('triggered')]
    print(f"Oblada {version}, {header.get('timestamp', '')}: {json.dumps(body.get('exception', {}))}")

    # One atos run for each image, with all its frames' addresses.
    names = {}
    wanted = {}
    for _, thread in threads:
        for frame in thread['frames']:
            image = images[frame['imageIndex']] if 'imageIndex' in frame else {}
            if image.get('uuid', '').lower() in dsyms:
                wanted.setdefault(frame['imageIndex'], set()).add(image['base'] + frame['imageOffset'])
    for index, addresses in wanted.items():
        image = images[index]
        addresses = sorted(addresses)
        out = subprocess.run(
            ['atos', '-o', dsyms[image['uuid'].lower()], '-arch', image.get('arch', 'arm64'),
             '-l', hex(image['base'])] + [hex(a) for a in addresses],
            capture_output=True, text=True, check=True).stdout.splitlines()
        names.update(((index, a), name) for a, name in zip(addresses, out))

    for number, thread in threads:
        crashed = ' (crashed)' if thread.get('triggered') else ''
        print(f"\nThread {number}{crashed} {thread.get('name') or thread.get('queue') or ''}")
        for depth, frame in enumerate(thread['frames']):
            index = frame.get('imageIndex')
            image = images[index] if index is not None else {}
            address = image.get('base', 0) + frame.get('imageOffset', 0)
            name = names.get((index, address))
            if not name and 'symbol' in frame:
                name = f"{frame['symbol']} + {frame.get('symbolLocation', 0)}"
            print(f"{depth:3}  {image.get('name', '???'):32} {name or hex(address)}")


if __name__ == '__main__':
    main()
