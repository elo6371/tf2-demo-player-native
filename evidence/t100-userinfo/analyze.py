#!/usr/bin/env python3
"""Summarise one demo_open_probe --scan --userinfo --userinfo-at run.

Reads the raw probe output, prints the userinfo fields plus the per-slot table,
and fails loudly when an invariant the decoder claims to hold does not.

The table's entry text is the client slot in decimal; the name lives in the
user data. A slot with no user data keeps an empty name on purpose: copying the
slot number into the name field would make an empty slot look like a player
called "18", which is the silent substitution this work exists to reject.
"""
import sys
import os

DEMO_FIELDS = [
    "userinfo_table_id", "userinfo_create", "userinfo_updates", "userinfo_entries",
    "userinfo_slots", "userinfo_samples", "userinfo_named", "userinfo_failures",
    "userinfo_unparsed", "userinfo_missing_userdata", "userinfo_empty_names",
    "userinfo_text_slot_mismatches", "userinfo_duplicate_userids",
    "userinfo_table_resets", "userinfo_fixed", "userinfo_fixed_bits",
    "userinfo_max_entries", "userinfo_max_userdata_bits", "userinfo_layout_mask",
]


def load(path):
    wide = None
    slots = []
    ats = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n").rstrip("\r")
            if "userinfo_table_id=" in line:
                wide = line
            elif line[:1].isdigit() and "\t" in line:
                # Slot rows start with the entry index; event rows ("event\t...")
                # and raw dumps ("userinfo_raw\t...") do not.
                slots.append(line.split("\t"))
            elif line.startswith("userinfo_at\t") or line.startswith("userinfo_at "):
                ats.append(line.split("\t"))
    fields = {}
    if wide:
        for token in wide.split(" "):
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = value
    return fields, slots, ats


def main():
    bad = 0
    for path in sys.argv[1:]:
        fields, slots, ats = load(path)
        name = os.path.basename(path)
        print("== %s" % name)
        for key in DEMO_FIELDS:
            print("   %s=%s" % (key, fields.get(key, "<missing>")))
        print("   rows=%d at_rows=%d" % (len(slots), len(ats)))
        seen_users = {}
        named = 0
        for row in slots:
            if len(row) < 8:
                print("   BAD-ROW %r" % (row,))
                bad += 1
                continue
            entry, entity, userid, player_name, layout = row[0], row[1], row[2], row[3], row[7]
            text = row[8] if len(row) > 8 else ""
            if entity != str(int(entry) + 1):
                print("   BAD-ENTITY entry=%s entity=%s" % (entry, entity))
                bad += 1
            if player_name:
                named += 1
                if layout != "0":
                    print("   BAD-LAYOUT named entry=%s layout=%s" % (entry, layout))
                    bad += 1
            else:
                # No user data: nothing was decoded, and nothing pretends to be.
                if layout != "255":
                    print("   BAD-LAYOUT unnamed entry=%s layout=%s" % (entry, layout))
                    bad += 1
                if text != entry:
                    print("   BAD-TEXT entry=%s text=%r" % (entry, text))
                    bad += 1
            if player_name and any(byte < 0x20 or byte == 0x7F for byte in player_name.encode("utf-8")):
                print("   BAD-NAME-CTRL entry=%s %r" % (entry, player_name))
                bad += 1
            if userid != "0":
                if userid in seen_users:
                    print("   DUP-USERID %s on entries %s and %s" % (userid, seen_users[userid], entry))
                    bad += 1
                seen_users[userid] = entry
        if "userinfo_named" in fields and fields["userinfo_named"] != str(named):
            print("   BAD-NAMED wide=%s rows=%d" % (fields["userinfo_named"], named))
            bad += 1
        for row in slots:
            print("   slot %s" % " | ".join(row[:9]))
        if ats:
            for row in ats:
                print("   at  %s" % " | ".join(row[1:7]))
    if bad:
        print("ANALYZE-BAD=%d" % bad)
        return 1
    print("ANALYZE-OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
