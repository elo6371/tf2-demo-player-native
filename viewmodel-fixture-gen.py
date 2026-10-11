#!/usr/bin/env python3
"""viewmodel-fixture-gen.py <tf-root>

Writes the synthetic ViewModel fixtures that viewmodel-contract-check.sh asserts
against. The positive fixture is a real game asset (`v_scattergun_scout.mdl`, read
out of the installed VPK); the negatives cannot be, and that is the point.

WHY SYNTHETIC FIXTURES ARE NEEDED FOR THE NEGATIVE CASES
--------------------------------------------------------
The plan asks the gate to prove that one damaged attachment does not take the
ViewModel down, and that a model whose companions are missing is refused. Neither
case exists in the shipped game: every weapon viewmodel in the VPK parses with
every attachment valid, and every one has its `.vvd` and `.dx90.vtx` next to it.
So there is no asset to point at, and "we looked and did not find one" is not a
control. The damaged case is a defensive branch, and a defensive branch with no
input that reaches it is a branch nobody has run.

These fixtures are deliberately tiny (a few hundred bytes each) and are built
here rather than committed as opaque blobs, so a reader can see exactly which
byte carries the defect. They are *not* complete models: no sequences, no
bodyparts, no decodable mesh. What they do have is the part under test -- a real
studiohdr_t word layout, a real bone table, and a real attachment table -- so the
parser that reads them is the same parser the product uses, reached through
`buildViewModelRequest`.

THE PAIR IS THE INSTRUMENT
--------------------------
`v_fixture_ok` and `v_fixture_damaged` are the same bytes except for one int32:
the attachment's bone index (0, in the skeleton, versus 99, outside it). If only
the damaged fixture were asserted, `invalidAttachments=1` would be satisfied by a
synthetic file the parser merely failed to understand. The control is what makes
the reading mean "the bone index was rejected" rather than "the fixture is
garbage". The same pairing discipline is why `v_fixture_noanchor` exists: its one
attachment is valid but is named `muzzle`, so it fails the *selection* rule rather
than the *classification* rule, and a build that silently took "the first
attachment" would be caught by it and by nothing else.

Offsets are the ones verified byte-for-byte against `scout.mdl` in
model_loader.cpp (232/236 bodyparts, 240/244 localattachments, 248/252
localnodes); the bone and attachment strides (216 / 92) are that file's
constants. Keeping them in sync by hand is the cost of not having a writer in the
C++ tree, and the gate's assertions are what notice if this file drifts.

Usage: python viewmodel-fixture-gen.py <tf-root>
       writes <tf-root>/models/weapons/v_models/<stem>.{mdl,vvd,dx90.vtx},
       plus the materials/ and maps/ placeholders that make <tf-root> a root the
       product will accept.
"""
import os
import struct
import sys

IDST = 0x54534449  # "IDST" -- studiohdr_t::id
IDVD = 0x56534449  # "IDSV" -- vertexfileheader_t::id
MDL_VERSION = 48   # >= 44 is what ModelLoader requires
HEADER_BYTES = 316  # parseMdl refuses anything shorter
BONE_STRIDE = 216
ATTACHMENT_STRIDE = 92
BONE_POSE_OFFSET = 96
VVD_BYTES = 64
VTX_BYTES = 64

# matrix3x4_t, row-major, translation in column 3. Written as 12 floats in file
# order: row0 (m00 m01 m02 m03), row1, row2.
IDENTITY34 = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)


def put_string(buf, offset, text):
    raw = text.encode("ascii")
    buf[offset:offset + len(raw)] = raw


def build_mdl(name, bones, attachments):
    """bones: [(name, parent)]  attachments: [(name, bone_index)]"""
    bone_table = HEADER_BYTES
    bone_names = bone_table + BONE_STRIDE * len(bones)
    attachment_table = bone_names + sum(len(n) + 1 for n, _ in bones)
    attachment_names = attachment_table + ATTACHMENT_STRIDE * len(attachments)
    total = attachment_names + sum(len(n) + 1 for n, _ in attachments)
    buf = bytearray(total)

    struct.pack_into("<III", buf, 0, IDST, MDL_VERSION, 0x0BADF00D)
    put_string(buf, 12, name)                      # szname[64], null padded
    struct.pack_into("<I", buf, 76, total)         # length -- must be <= size
    struct.pack_into("<I", buf, 152, 0)            # flags
    struct.pack_into("<II", buf, 156, len(bones), bone_table)
    struct.pack_into("<II", buf, 188, 0, 0)        # numlocalseq / sequenceindex
    struct.pack_into("<II", buf, 204, 0, 0)        # numtextures / textureindex
    struct.pack_into("<II", buf, 212, 0, 0)        # numcdtextures / cdtextureindex
    struct.pack_into("<II", buf, 232, 0, 0)        # numbodyparts / bodypartindex
    struct.pack_into("<II", buf, 240, len(attachments), attachment_table)
    struct.pack_into("<II", buf, 248, 0, 0)        # numlocalnodes / localnodeindex

    cursor = bone_names
    for index, (bone_name, parent) in enumerate(bones):
        base = bone_table + index * BONE_STRIDE
        struct.pack_into("<i", buf, base, cursor - base)      # sznameindex
        struct.pack_into("<i", buf, base + 4, parent)         # parent
        struct.pack_into("<fff", buf, base + 32, 0.0, 0.0, 0.0)  # pos
        for slot, value in enumerate(IDENTITY34):
            struct.pack_into("<f", buf, base + BONE_POSE_OFFSET + 4 * slot, value)
        put_string(buf, cursor, bone_name)
        cursor += len(bone_name) + 1

    cursor = attachment_names
    for index, (attachment_name, bone) in enumerate(attachments):
        base = attachment_table + index * ATTACHMENT_STRIDE
        struct.pack_into("<i", buf, base, cursor - base)  # sznameindex
        struct.pack_into("<i", buf, base + 4, 0)          # flags
        struct.pack_into("<i", buf, base + 8, bone)       # localbone
        for slot, value in enumerate(IDENTITY34):
            struct.pack_into("<f", buf, base + 12 + 4 * slot, value)
        put_string(buf, cursor, attachment_name)
        cursor += len(attachment_name) + 1

    return bytes(buf)


def build_vvd():
    """A VVD header with zero vertices. inspectFile only needs the id and a
    checksum, but parseVvd reads version/numLod0/vertexDataStart, so those are
    consistent with a 64-byte file rather than left at zero."""
    buf = bytearray(VVD_BYTES)
    struct.pack_into("<I", buf, 0, IDVD)
    struct.pack_into("<I", buf, 4, 4)             # version 4..5
    struct.pack_into("<I", buf, 8, 0x1D2C3B4A)    # checksum, non-zero
    struct.pack_into("<I", buf, 12, 1)            # numLODs
    struct.pack_into("<I", buf, 16, 0)            # numLOD0 vertices
    struct.pack_into("<I", buf, 56, VVD_BYTES)    # vertexDataStart
    return bytes(buf)


def build_vtx():
    """A VTX header with no mesh. Source VTX files put the version first (there
    is no IDST/IDSV tag) and the checksum at +16; inspectFile wants a version in
    6..8 and a non-zero checksum. A model with a header but no hierarchy is a
    model whose mesh cannot be decoded, which is the state these fixtures are in
    on purpose -- the gate asserts the attachment and selection rules, not the
    mesh, and `meshReady=false` is recorded rather than hidden."""
    buf = bytearray(VTX_BYTES)
    struct.pack_into("<I", buf, 0, 7)             # version 6..8
    struct.pack_into("<I", buf, 16, 0x5A5A5A5A)   # checksum, non-zero
    return bytes(buf)


# stem, bones, attachments, companions
FIXTURES = (
    # The control. One bone, one attachment, and the attachment sits on bone 0,
    # which is inside the skeleton. Every rule passes.
    ("v_fixture_ok", (("weapon_bone", -1),), (("weapon_bone", 0),), True),
    # The defect, one int32 away from the control: bone 99 of a one-bone
    # skeleton. Must be recorded as invalid, must not fail the request, and must
    # not be used for placement.
    ("v_fixture_damaged", (("weapon_bone", -1),), (("weapon_bone", 99),), True),
    # Valid attachment, wrong name. The selection rule must refuse to place the
    # weapon on it: "the first attachment" is not a placement policy.
    ("v_fixture_noanchor", (("weapon_bone", -1),), (("muzzle", 0),), True),
    # An MDL with no companions at all. The request must be refused before the
    # MDL is even inspected.
    ("v_fixture_nocompanion", (("weapon_bone", -1),), (("weapon_bone", 0),), False),
)

# `AssetRoot::valid()` -- the predicate the *product* uses before it will accept a
# `--tf-root` -- requires all three asset stores to be present: materials, models
# and maps. The fixtures only need `models`, but a root without the other two is
# not a root the product will use: it drops the operator's `--tf-root` on the
# floor and reports `viewmodel was not found`, which points at the model instead
# of at the root. That is exactly what this gate's first run did, and it read as a
# missing asset rather than as a rejected root. Two placeholder files make the
# fixture root a root the product accepts, so the fixture bytes are what gets
# read. (git does not track empty directories, so the placeholders are also what
# keeps the two stores in the repository.)
PLACEHOLDERS = (
    ("materials", ".gitkeep", "placeholder so AssetRoot::valid() accepts this fixture root\n"),
    ("maps", ".gitkeep", "placeholder so AssetRoot::valid() accepts this fixture root\n"),
)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: viewmodel-fixture-gen.py <tf-root>\n")
        return 2
    root = argv[1]
    out_dir = os.path.join(root, "models", "weapons", "v_models")
    os.makedirs(out_dir, exist_ok=True)
    for store, name, body in PLACEHOLDERS:
        store_dir = os.path.join(root, store)
        os.makedirs(store_dir, exist_ok=True)
        with open(os.path.join(store_dir, name), "w", encoding="ascii", newline="\n") as handle:
            handle.write(body)
    for stem, bones, attachments, companions in FIXTURES:
        mdl = build_mdl(stem + ".mdl", bones, attachments)
        with open(os.path.join(out_dir, stem + ".mdl"), "wb") as handle:
            handle.write(mdl)
        if companions:
            with open(os.path.join(out_dir, stem + ".vvd"), "wb") as handle:
                handle.write(build_vvd())
            with open(os.path.join(out_dir, stem + ".dx90.vtx"), "wb") as handle:
                handle.write(build_vtx())
        print("wrote %s/%s.mdl (%d bytes, companions=%s)"
              % (out_dir.replace(os.sep, "/"), stem, len(mdl), int(companions)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
