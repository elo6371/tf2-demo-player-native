#!/usr/bin/env python3
"""install-lifecycle-fixtures.py -- T37 (2026-10-20): the resource-readiness
roots the installer gate drives the installed product against.

WHY A SECOND FIXTURE SCRIPT
   install-readiness-fixtures.py (T32) builds the eight roots its thirteen cases
   assert, and those roots are byte-for-byte inputs to accepted readings. Adding
   roots to it would change `fixture_roots=8` and the committed SHA-256 list for
   no reason. These roots answer the T37 questions instead, and they are built
   the same way -- from the pinned demo and the pinned map, never from this
   machine's Steam install:

   vpk-missing       markers only; no pak01_dir.vpk at all. The product must
                     report NO refusal (an absent archive is the normal case)
                     and store=none. This is the case that separates
                     'absent' from 'present but unreadable'.
   vpk-unreadable    pak01_dir.vpk exists and its DACL denies read to the
                     running user, so the file is present and unopenable --
                     the path/permission hypothesis for pak01. The product
                     must name the reader's reason, not 'not found'.
   vpk-format        a well-formed file with signature 0x55aa1234 and
                     version 3: an unsupported VPK FORMAT. Distinct reason.
   vpk-truncated-tree  a valid v1 header whose declared tree runs past EOF.
   vpk-valid         a real BSP, inline in the archive: the positive control.
   vpk-volume-missing  the tree names a numbered volume that is not on disk.
                     open() succeeds and read() fails, which the map lookup
                     surfaces as store=none with no per-entry reason -- the
                     limitation T37 records rather than hides.
   precedence-shadow  maps/<marker>.bsp is NOT a BSP while the same name in
                     pak01_dir.vpk IS. T32's precedence root proves 'a valid
                     loose file beats a bad archive'; this one proves the
                     stronger half: a BAD loose file still shadows a VALID
                     archived one. A product that merely preferred whichever
                     copy could be parsed would answer store=vpk here.

The unreadable root is the only fixture that needs a non-file change (a deny
ACE), so the script also removes it on request with --reset-acl: deleting a
directory whose file is denied to us is itself a permission question.

USAGE
   python install-lifecycle-fixtures.py --out DIR --demo DEMO --bsp MAP [--reset-acl]
Exit 0 when every root and the demo rewrite were written and hashed.
"""

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys

SIGNATURE = 0x55AA1234
DIRECTORY_ARCHIVE = 0x7FFF
MAP_NAME_FIELD_OFFSET = 536
MAP_NAME_FIELD_LENGTH = 260
FIXTURE_MAP = 't32_marker'


def put32(buffer, value):
    buffer.extend(struct.pack('<I', value & 0xFFFFFFFF))


def put16(buffer, value):
    buffer.extend(struct.pack('<H', value & 0xFFFF))


def put_string(buffer, value):
    buffer.extend(value.encode('ascii'))
    buffer.append(0)


def build_archive(files, trailing=b'', version=1):
    """One directory tree plus optional trailing inline data."""
    tree = bytearray()
    for spec in files:
        put_string(tree, spec['extension'])
        put_string(tree, spec['path'])
        put_string(tree, spec['filename'])
        put32(tree, 0)
        put16(tree, 0)
        put16(tree, spec.get('archive_index', DIRECTORY_ARCHIVE))
        put32(tree, spec['offset'])
        put32(tree, spec['length'])
        put16(tree, 0xFFFF)
        tree.append(0)
        tree.append(0)
    tree.append(0)
    out = bytearray()
    put32(out, SIGNATURE)
    put32(out, version)
    put32(out, len(tree))
    if version == 2:
        put32(out, 0)
        put32(out, 0)
        put32(out, 0)
        put32(out, 0)
    out.extend(tree)
    out.extend(trailing)
    return bytes(out)


def inline_archive(entries, version=1):
    files = []
    data = bytearray()
    for path, filename, payload in entries:
        files.append({'extension': 'bsp', 'path': path, 'filename': filename,
                      'offset': len(data), 'length': len(payload)})
        data.extend(payload)
    return build_archive(files, trailing=bytes(data), version=version)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as handle:
        handle.write(data)


def write_markers(root):
    write(os.path.join(root, 'materials', 't32', 'marker.vmt'),
          b'"UnlitGeneric"\n{\n\t"$basetexture" "t32/marker"\n}\n')
    write(os.path.join(root, 'models', 't32', 'marker.mdl'), b'T37-LIFECYCLE-MODEL-MARKER')
    write(os.path.join(root, 'materials', 't32', 'marker.vtf'), b'T37-LIFECYCLE-TEXTURE-MARKER')


def deny_read(path):
    """A present-but-unreadable file: the path/permission hypothesis."""
    user = os.environ.get('USERNAME', '')
    if not user:
        raise SystemExit('cannot name the current user for the deny ACE')
    result = subprocess.run(['icacls', path, '/deny', '%s:(R)' % user],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit('icacls deny failed: %s %s' % (result.stdout, result.stderr))


def reset_acl(root):
    user = os.environ.get('USERNAME', '')
    if not user or not os.path.isdir(root):
        return
    subprocess.run(['icacls', root, '/remove:d', user, '/T', '/C'],
                   capture_output=True, text=True)


def rewrite_demo(source, destination, map_name):
    with open(source, 'rb') as handle:
        data = bytearray(handle.read())
    if len(data) < MAP_NAME_FIELD_OFFSET + MAP_NAME_FIELD_LENGTH:
        raise SystemExit('demo is shorter than an HL2DEMO header: %s' % source)
    name = map_name.encode('ascii')
    if len(name) >= MAP_NAME_FIELD_LENGTH:
        raise SystemExit('fixture map name does not fit the header field')
    data[MAP_NAME_FIELD_OFFSET:MAP_NAME_FIELD_OFFSET + MAP_NAME_FIELD_LENGTH] = \
        name + b'\x00' * (MAP_NAME_FIELD_LENGTH - len(name))
    write(destination, bytes(data))


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True)
    parser.add_argument('--demo', required=True)
    parser.add_argument('--bsp', required=True)
    parser.add_argument('--reset-acl', action='store_true')
    args = parser.parse_args(argv)
    out = os.path.abspath(args.out)
    if args.reset_acl:
        reset_acl(out)
        return 0
    if os.path.isdir(out):
        reset_acl(out)
        shutil.rmtree(out)
    os.makedirs(out)

    with open(args.bsp, 'rb') as handle:
        bsp = handle.read()
    if len(bsp) < 8 or bsp[:4] != b'VBSP':
        raise SystemExit('input map is not a VBSP file: %s' % args.bsp)

    demo = os.path.join(out, 'demo', FIXTURE_MAP + '.dem')
    rewrite_demo(args.demo, demo, FIXTURE_MAP)

    roots = os.path.join(out, 'roots')
    map_relative = os.path.join('maps', FIXTURE_MAP + '.bsp')

    # 1. missing: no archive at all. Absent is not a refusal.
    root = os.path.join(roots, 'vpk-missing')
    write_markers(root)
    write(os.path.join(root, map_relative), bsp)

    # 2. unreadable: present, and the DACL says no.
    root = os.path.join(roots, 'vpk-unreadable')
    write_markers(root)
    archive = os.path.join(root, 'pak01_dir.vpk')
    write(archive, inline_archive([('maps', FIXTURE_MAP, bsp)]))
    deny_read(archive)

    # 3. format: signature fine, version 3 -- a format this reader does not know.
    root = os.path.join(roots, 'vpk-format')
    write_markers(root)
    tree = bytearray()
    put_string(tree, 'bsp'); put_string(tree, 'maps'); put_string(tree, FIXTURE_MAP)
    put32(tree, 0); put16(tree, 0); put16(tree, DIRECTORY_ARCHIVE)
    put32(tree, 0); put32(tree, 4); put16(tree, 0xFFFF)
    tree.append(0); tree.append(0); tree.append(0)
    header = bytearray()
    put32(header, SIGNATURE); put32(header, 3); put32(header, len(tree))
    write(os.path.join(root, 'pak01_dir.vpk'), bytes(header) + bytes(tree))

    # 4. truncated tree: a valid header whose tree runs past EOF.
    root = os.path.join(roots, 'vpk-truncated-tree')
    write_markers(root)
    header = bytearray()
    put32(header, SIGNATURE); put32(header, 1); put32(header, 4096)
    write(os.path.join(root, 'pak01_dir.vpk'), bytes(header) + b'\x00' * 16)

    # 5. valid: the positive control.
    root = os.path.join(roots, 'vpk-valid')
    write_markers(root)
    write(os.path.join(root, 'pak01_dir.vpk'),
          inline_archive([('maps', FIXTURE_MAP, bsp)]))

    # 6. volume missing: open() succeeds, read() cannot find the volume.
    root = os.path.join(roots, 'vpk-volume-missing')
    write_markers(root)
    write(os.path.join(root, 'pak01_dir.vpk'), build_archive([
        {'extension': 'bsp', 'path': 'maps', 'filename': FIXTURE_MAP,
         'offset': 0, 'length': len(bsp), 'archive_index': 0},
    ]))

    # 7. precedence shadow: a BAD loose map in front of a GOOD archived one.
    root = os.path.join(roots, 'precedence-shadow')
    write_markers(root)
    write(os.path.join(root, map_relative), b'NOT-A-BSP-T37-LOOSE-SHADOW-PAYLOAD')
    write(os.path.join(root, 'pak01_dir.vpk'),
          inline_archive([('maps', FIXTURE_MAP, bsp)]))

    hashes = []
    for directory, _subdirs, filenames in os.walk(out):
        for filename in sorted(filenames):
            path = os.path.join(directory, filename)
            relative = os.path.relpath(path, out).replace('\\', '/')
            # The denied archive is not readable by us either; it is hashed
            # from the bytes that were written, before the ACE was applied.
            try:
                size = os.path.getsize(path)
                digest = sha256_of(path)
            except OSError:
                size, digest = -1, 'unreadable-by-design'
            hashes.append((relative, size, digest))
    hashes.sort()
    with open(os.path.join(out, 'inputs.sha256'), 'w', encoding='ascii', newline='\n') as handle:
        for relative, size, digest in hashes:
            handle.write('%s  %d  %s\n' % (digest, size, relative))

    print('fixtures_root=%s' % out)
    print('fixture_demo=%s' % demo)
    print('fixture_roots=%d' % len(os.listdir(roots)))
    print('fixture_files=%d' % len(hashes))
    for relative, size, digest in hashes:
        if relative.startswith('roots/') and relative.endswith('_dir.vpk'):
            print('fixture_vpk=%s bytes=%d sha256=%s' % (relative, size, digest))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
