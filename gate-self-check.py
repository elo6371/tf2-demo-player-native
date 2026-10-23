#!/usr/bin/env python3
"""Ask a gate two questions about its own assertions.

Gates in this repo assert on tokens like "urlRefused=26" and "refusalReason=not-https".
Two failure modes hide in that style, and neither one needs the product to run to
find:

  trap 1  the assertion's object does not exist on the success path.
          A probe-expectation key that no readings.set() ever writes can never
          match, and a gate that greps for it reports FAIL forever (or, worse,
          reports OK because the variable was empty and the comparison was too).

  trap 9  the asserted string exists and can never equal the expectation.
          T54 asserted "refusal=host-not-allowed" against a field that holds the
          human sentence; the substring "refusal=" is in the log, so the
          assertion looks anchored, and it was unsatisfiable from the day it was
          written. These are found by reading the writer, which is what --sources
          does: every asserted key must appear in a format string in the sources.

Usage:
  gate-self-check.py --gate logstf-check.sh \
                     --readings native/tools/logstf_viewer_probe.cpp \
                     --sources native/src

  --gate      the gate to read assertions from (required)
  --readings  a probe source whose readings.set("name")/readings.add("name") calls
              are the only keys the gate's expectation table may use (optional)
  --sources   a directory of sources that must contain every "key=" the gate
              asserts on (optional; searches *.cpp and *.h)

Exit: 0 nothing found, 1 findings, 2 usage error. Findings print one per line with
the gate line number, so the fix is a jump, not a search.
"""
import argparse
import glob
import os
import re
import sys


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def gate_expectation_keys(gate_text):
    """(line, key, value) of a 'key=value|description' expectation table."""
    found = []
    for index, line in enumerate(gate_text.splitlines(), start=1):
        match = re.match(r"\s*'([A-Za-z][A-Za-z0-9_]*)=([^|]*)\|", line)
        if match:
            found.append((index, match.group(1), match.group(2).strip(), line.strip()))
    return found


def probe_readings(source_text):
    """name -> how many times the probe sets it.

    A name set twice is a reading whose earlier value the gate can never see: the
    second set() overwrites it. T54's "reopenState" was set by the empty-panel
    case and again by the loaded-panel case, so the gate's "a reopen is Empty
    again" assertion read "content" forever -- trap 9 one layer down, and the
    reason this check is in the tool rather than in a reviewer's head.
    """
    counts = {}
    for name in re.findall(r'readings\.(?:set|add)\("([^"]+)"', source_text):
        counts[name] = counts.get(name, 0) + 1
    return counts


def asserted_tokens(gate_text):
    """(line, key) for every token=value the gate passes to assert_contains."""
    found = []
    pattern = re.compile(r'assert_contains\s+"[^"]*"\s+"[^"]*"\s+"([^"]+)"')
    for index, line in enumerate(gate_text.splitlines(), start=1):
        for match in pattern.finditer(line):
            for part in match.group(1).split():
                if "=" in part:
                    found.append((index, part.split("=", 1)[0]))
    return found


def main(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--gate", required=True)
    parser.add_argument("--readings")
    parser.add_argument("--sources")
    args = parser.parse_args(argv)

    gate_text = read(args.gate)
    findings = 0
    # A tool that checks nothing must not print PASS: "0 items checked" and "all
    # items OK" are different answers, and this script exists because gates made
    # that mistake. T51's gate, for instance, asserts with assert_eq and has no
    # literal key=value tokens at all -- there it can only say INCONCLUSIVE.
    checked = 0

    if args.readings:
        table = gate_expectation_keys(gate_text)
        keys = [(line, key) for line, key, _value, _text in table]
        available = probe_readings(read(args.readings))
        # Two expectations for one key with different values cannot both hold: the
        # probe prints one value per name. T54 shipped exactly that -- "a reopen is
        # Empty again" (reopenState=empty) and a second "lands on Content"
        # assertion still asking for reopenState=content -- after a rename that
        # only one of the two lines followed. The gate went red for a defect in
        # the gate, one run after the same class of defect was written up.
        by_key = {}
        for line, key, value, _text in table:
            by_key.setdefault(key, []).append((line, value))
        for key, entries in sorted(by_key.items()):
            values = sorted({value for _line, value in entries})
            if len(values) > 1:
                where = ", ".join(f"line {line} wants '{value}'" for line, value in entries)
                print(f"{args.gate}:{key}: contradictory expectations -- {where}")
                findings += 1
        for line, key in keys:
            if key not in available:
                print(f"{args.gate}:{line}: assertion key '{key}' is never set by "
                      f"{os.path.basename(args.readings)} -- this assertion cannot pass")
                findings += 1
        duplicates = sorted(name for name, count in available.items() if count > 1)
        for name in duplicates:
            print(f"{os.path.basename(args.readings)}: reading '{name}' is set "
                  f"{available[name]} times -- the later value overwrites the earlier one")
            findings += 1
        checked += len(keys) + len(duplicates)
        print(f"readings: {len(keys)} expectation keys checked against "
              f"{len(available)} distinct names; "
              f"{len(duplicates)} name(s) set more than once")

    if args.sources:
        sources = ""
        for pattern in ("*.cpp", "*.h"):
            for path in sorted(glob.glob(os.path.join(args.sources, pattern))):
                sources += read(path)
        seen = set()
        tokens = asserted_tokens(gate_text)
        for line, key in tokens:
            if key in seen:
                continue
            seen.add(key)
            if (key + "=") not in sources:
                print(f"{args.gate}:{line}: asserts '{key}=' but no string in "
                      f"{args.sources} writes it -- the token is being asserted on "
                      f"something that does not name it")
                findings += 1
        checked += len(seen)
        print(f"sources: {len(seen)} asserted keys checked against {args.sources}")

    if findings:
        print(f"GATE-SELF-CHECK=FAIL ({findings} finding(s))")
        return 1
    if checked == 0:
        print("GATE-SELF-CHECK=INCONCLUSIVE (no expectation table and no literal "
              "key=value token in this gate: nothing was checked)")
        return 0
    print(f"GATE-SELF-CHECK=PASS ({checked} item(s) checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
