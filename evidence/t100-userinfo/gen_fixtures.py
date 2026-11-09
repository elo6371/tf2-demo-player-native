#!/usr/bin/env python3
"""Synthetic svc_CreateStringTable fixtures for the userinfo string table.

The nine local demos only carry the userinfo layouts they happen to contain, and
none of them carries a damaged one. These fixtures are bit-exact message streams
fed to the SHIPPED decoder through decodeDemoMessageStream() (the same entry point
scanKnownDemoMessages uses), so the gate asserts about the real decode path.

Wire order is MessageBits' order: bit i of the stream is bit (i % 8) of byte
(i / 8), i.e. LSB-first within each byte. Strings are bytes up to a NUL.

A fixture is:  6-bit message type (12 = svc_CreateStringTable) | create body
The body:      name string | maxEntries(16) | entryCount(indexBits+1 then 1) |
               payloadBits(varint7, protocol > 23) | fixedUserData(1)=0 |
               compressed(1)=0 | payload bits
An entry:      sequential(1) | [index(indexBits) when not sequential] |
               hasText(1) [ hasHistory(1) historyIndex(5) copyCount(5) rest |
                            string ] | hasUserData(1) | 14-bit userBytes |
               user data bits
"""
import os
import struct
import sys

PLAYER_INFO_NAME = 0
PLAYER_INFO_USERID = 32
PLAYER_INFO_GUID = 36
PLAYER_INFO_FRIENDS_ID = 72
PLAYER_INFO_FRIENDS_NAME = 76
PLAYER_INFO_FAKE = 108
PLAYER_INFO_HLTV = 109
PLAYER_INFO_CUSTOM_FILES = 112
PLAYER_INFO_FILES_DOWNLOADED = 128
PLAYER_INFO_SIZE = 132          # public/cdll_int.h, padding included


def _fixed(text, size):
    raw = text.encode("utf-8")
    if len(raw) >= size:
        raise ValueError("field too long: %r" % text)
    return raw + b"\x00" * (size - len(raw))


def player_info(name, userid, guid, friends_id=0, friends_name="",
                fake=False, hltv=False, name_corrupt=None):
    raw = bytearray(PLAYER_INFO_SIZE)
    field = _fixed(name, 32)
    if name_corrupt is not None:
        field = name_corrupt + field[len(name_corrupt):]
    raw[PLAYER_INFO_NAME:PLAYER_INFO_NAME + 32] = field
    raw[PLAYER_INFO_USERID:PLAYER_INFO_USERID + 4] = struct.pack("<I", userid)
    raw[PLAYER_INFO_GUID:PLAYER_INFO_GUID + 33] = _fixed(guid, 33)
    raw[PLAYER_INFO_FRIENDS_ID:PLAYER_INFO_FRIENDS_ID + 4] = struct.pack("<I", friends_id)
    raw[PLAYER_INFO_FRIENDS_NAME:PLAYER_INFO_FRIENDS_NAME + 32] = _fixed(friends_name, 32)
    raw[PLAYER_INFO_FAKE] = 1 if fake else 0
    raw[PLAYER_INFO_HLTV] = 1 if hltv else 0
    raw[PLAYER_INFO_CUSTOM_FILES:PLAYER_INFO_CUSTOM_FILES + 4] = b"\x00" * 4
    raw[PLAYER_INFO_FILES_DOWNLOADED:PLAYER_INFO_FILES_DOWNLOADED + 4] = b"\x00" * 4
    return bytes(raw)


class BitWriter:
    def __init__(self):
        self.bytes = bytearray()
        self.bit = 0

    def write(self, value, width):
        for i in range(width):
            if self.bit % 8 == 0:
                self.bytes.append(0)
            self.bytes[-1] |= ((value >> i) & 1) << (self.bit % 8)
            self.bit += 1

    def write_string(self, text):
        for byte in text.encode("utf-8"):
            self.write(byte, 8)
        self.write(0, 8)

    def write_varint(self, value):
        while True:
            byte = value & 0x7F
            value >>= 7
            if value:
                self.write(byte | 0x80, 8)
            else:
                self.write(byte, 8)
                return

    def write_bits(self, data):
        for byte in data:
            self.write(byte, 8)


def index_bits(max_entries):
    # Mirrors src/demo_header_strtab.cpp indexBitsForMaxEntries: ceil(log2(n)) is
    # the wire width, and exact powers of two take one more bit than the old loop.
    bits = 0
    while (1 << bits) < max(1, max_entries) and bits < 31:
        bits += 1
    return bits


def table_payload(entries, max_entries):
    bits = index_bits(max_entries)
    out = BitWriter()
    last_index = -1
    for index, text, user_data in entries:
        if index == last_index + 1:
            out.write(1, 1)
        else:
            out.write(0, 1)
            out.write(index, bits)
        last_index = index
        out.write(1, 1)             # hasText
        out.write(0, 1)             # no history copy
        out.write_string(text)
        if user_data is None:
            out.write(0, 1)
        else:
            out.write(1, 1)
            out.write(len(user_data), 14)
            out.write_bits(user_data)
    return bytes(out.bytes), out.bit


def create_message(table_name, entries, max_entries, payload_cut_bits=0):
    payload, payload_bits = table_payload(entries, max_entries)
    payload_bits -= payload_cut_bits
    if payload_bits < 0:
        payload_bits = 0
    out = BitWriter()
    out.write(12, 6)                                   # svc_CreateStringTable
    out.write_string(table_name)
    out.write(max_entries, 16)
    out.write(len(entries), index_bits(max_entries) + 1)
    out.write_varint(payload_bits)
    out.write(0, 1)                                    # fixedUserData
    out.write(0, 1)                                    # compressed
    for i in range(payload_bits):
        out.write((payload[i // 8] >> (i % 8)) & 1, 1)
    return bytes(out.bytes), out.bit


def build(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    # Entry text is the client slot in decimal, which is what the nine corpus
    # demos carry; the player name lives only in the user data. Writing the name
    # into the text would make the fixture agree with a decoder that reads the
    # wrong half.
    entries = [
        (0, "0", player_info("Alpha", 7, "STEAM_0:1:12345", 111, "FriendA")),
        (1, "1", player_info("Bravo", 9, "BOT", 0, "")),
    ]
    written = []

    good, bits = create_message("userinfo", entries, 32)
    path = os.path.join(out_dir, "userinfo-good.demmsg")
    open(path, "wb").write(good)
    written.append((path, len(good), bits, "good"))

    # Declared lengths still describe the full table, but the bytes past the cut
    # are physically absent: the decoder must run out and count a failure rather
    # than report a shorter table as a complete one.
    cut_bytes = 40
    truncated = good[:-cut_bytes]
    path = os.path.join(out_dir, "userinfo-truncated.demmsg")
    open(path, "wb").write(truncated)
    written.append((path, len(truncated), bits - cut_bytes * 8, "truncated"))

    # Framing intact, declared payloadBits honest, but the payload stops inside the
    # second entry (exactly its 132-byte user data is missing). The outer length
    # check passes and the walk runs out mid-entry: that is the silent-empty shape
    # the gate must see as a failure count, not as a one-entry table.
    payload, payload_bits = table_payload(entries, 32)
    second_userdata_bits = PLAYER_INFO_SIZE * 8
    short_cut = second_userdata_bits
    short, short_bits = create_message("userinfo", entries, 32, payload_cut_bits=short_cut)
    path = os.path.join(out_dir, "userinfo-shortpayload.demmsg")
    open(path, "wb").write(short)
    written.append((path, len(short), short_bits, "shortpayload"))

    # Same framing, same lengths, but both name fields start with a control byte:
    # the struct is present and its userid/guid still parse, so this must show up
    # as unparsed entries, not as a silent empty table and not as a name.
    bad = [
        (0, "0", player_info("Alpha", 7, "STEAM_0:1:12345", 111, "FriendA",
                             name_corrupt=b"\x01\x02Alpha")),
        (1, "1", player_info("Bravo", 9, "BOT", 0, "",
                             name_corrupt=b"\x01\x02Bravo")),
    ]
    corrupt, bits = create_message("userinfo", bad, 32)
    path = os.path.join(out_dir, "userinfo-badname.demmsg")
    open(path, "wb").write(corrupt)
    written.append((path, len(corrupt), bits, "badname"))

    for path, size, bits, label in written:
        print("%s\t%d\t%d" % (label, size, bits))


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else "evidence/t100-userinfo/fixtures")
