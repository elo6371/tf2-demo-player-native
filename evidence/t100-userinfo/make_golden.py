#!/usr/bin/env python3
"""Turn probe --userinfo output into the corpus snapshots the gate compares against.

Usage: python make_golden.py <raw-dir> <golden-dir> <demo> [<demo> ...]

The snapshot is the slot table (entry/entity/userId/name/firstTick/lastTick/
updates/layoutId/tableText) sorted by entry index, exactly the columns
userinfo-check.sh writes into its rows file. It is a snapshot of one frozen demo
file, not a claim about live servers: when the decoder changes, a diff here is
the review surface.
"""
import os
import sys


def rows(raw_path):
    out = []
    with open(raw_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n").rstrip("\r")
            if not line or not line[0].isdigit():
                continue
            fields = line.split("\t")
            if len(fields) < 9:
                raise SystemExit("short row in %s: %r" % (raw_path, fields))
            out.append(fields[:9])
    out.sort(key=lambda fields: int(fields[0]))
    return out


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    raw_dir, golden_dir, demos = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(golden_dir, exist_ok=True)
    for demo in demos:
        raw_path = os.path.join(raw_dir, demo + ".txt")
        if not os.path.isfile(raw_path):
            raise SystemExit("missing raw reading %s" % raw_path)
        table = rows(raw_path)
        if not table:
            raise SystemExit("no slot rows in %s -- refusing to write an empty snapshot" % raw_path)
        path = os.path.join(golden_dir, demo + ".tsv")
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            for fields in table:
                handle.write("\t".join(fields) + "\n")
        named = sum(1 for fields in table if fields[3])
        layouts = sorted({fields[7] for fields in table})
        print("%s rows=%d named=%d layouts=%s -> %s" % (demo, len(table), named, ",".join(layouts), path))


if __name__ == "__main__":
    main()
