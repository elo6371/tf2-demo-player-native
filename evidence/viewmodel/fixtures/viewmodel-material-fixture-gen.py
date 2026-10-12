#!/usr/bin/env python3
"""viewmodel-material-fixture-gen.py <vmfix-root>

Writes the synthetic ViewModel material fixtures that viewmodel-material-check.sh
asserts against. Separate root from the contract fixtures (`fixtures/tf`) on
purpose: viewmodel-contract-check.sh diffs its own fixture root with `diff -r`
against a fresh regeneration, so files added under `fixtures/tf` would redden
that gate. These fixtures live under `fixtures/vmfix-tf` and are regenerated and
byte-compared by the material gate itself.

WHY SYNTHETIC FIXTURES
----------------------
The task asks the gate to prove the resolver reports missing VMTs, missing VTFs,
wrong $cdmaterials, backslash paths, oversized textures, BC1/BC3 decodes and
unusable paths. The shipped game has none of the failure shapes: every real
ViewModel material resolves, and a resolver that never fails is a resolver with
no witness. Each fixture below is one defect class, and the control for each is
either a sibling fixture (same stem, one byte-class away) or the positive
fixture itself.

THE MDL BYTES
-------------
Same layout viewmodel-fixture-gen.py uses, verified word for word against
model_loader.cpp: 316-byte studiohdr_t; 156/160 bones; 204/208 numtextures /
textureindex (mstudiotexture_t stride 64, name index relative to the entry);
212/216 numcdtextures / cdtextureindex (int32 ABSOLUTE file offsets to the
directory strings -- reading them relative was a real defect this repo already
paid for); 232/236 bodyparts; 240/244 attachments. The fixture carries one bone
and no attachments: the material chain does not read either table, and a model
whose only tables are the ones under test cannot pass for the wrong reason.

THE VTF BYTES
-------------
Header offsets are the ones vtf_texture.cpp reads: signature 0, version 4/8,
headerSize 12, width 16, height 18, flags 20, frames 24, format 52, mipCount 56,
low-res 57/61/62, depth 63 (7.2+), image data at headerSize. No 7.3+ resource
table, so mipStart == headerSize. Version 7.2 with mipCount 1 keeps the chain
exactly one mip, which is what makes the oversized fixture's arithmetic small
enough to commit.

Usage: python viewmodel-material-fixture-gen.py <vmfix-root>
"""
import os
import struct
import sys

IDST = 0x54534449  # "IDST" -- studiohdr_t::id
MDL_VERSION = 48   # parseMdl requires >= 44
HEADER_BYTES = 316
BONE_STRIDE = 216
TEXTURE_STRIDE = 64
BONE_POSE_OFFSET = 96

IDENTITY34 = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)

# The positive shape, parameterised by stem: every fixture that must resolve to
# its *own* material needs its own $basetexture. The first version of this file
# hard-coded the ok stem in a shared constant, so the missing-VTF and oversized
# fixtures silently resolved through vmfix_ok's texture and read green -- a
# fixture that passes for the wrong reason is worse than no fixture.
VMT_OK = """"VertexLitGeneric"
{
\t"$basetexture" "models/weapons/v_models/vmfix/{stem}"
\t"$bumpmap" "models/weapons/v_models/vmfix/{stem}_normal"
\t"$phong" "1"
\t"$phongexponent" "15"
\t"$halflambert" "1"
\t"Proxies"
\t{
\t\t"Equals"
\t\t{
\t\t\t"srcVar1" "$yellow"
\t\t\t"resultVar" "$color2"
\t\t}
\t}
}
"""


def ok_vmt(stem):
    # replace, not format: the VMT body's own braces are not field names.
    return VMT_OK.replace("{stem}", stem)

# The real v_scattergun.vmt carries "$basetexture" "models\\weapons\\v_scattergun\\v_scattergun".
# Backslashes are path separators in a VMT value, not escapes; this fixture is
# that shape on purpose.
VMT_BACKSLASH = """"VertexLitGeneric"
{
\t"$basetexture" "models\\weapons\\v_models\\vmfix\\vmfix_backslash"
\t"$phong" "1"
}
"""

# Self-illum and translucent with no phong and no bumpmap, so the feature
# readings have a second shape to be told apart from the positive fixture.
VMT_DXT5 = """"VertexLitGeneric"
{
\t"$basetexture" "models/weapons/v_models/vmfix/vmfix_dxt5"
\t"$selfillum" "1"
\t"$translucent" "1"
}
"""

# A material that names a texture outside the asset root. resourcePath() strips
# the materials/ prefix and appends .vtf, giving materials/../../vmfix_escape.vtf;
# AssetRoot::resolve() must refuse it rather than read it.
VMT_ESCAPE = """"VertexLitGeneric"
{
\t"$basetexture" "materials/../../vmfix_escape"
}
"""

# Formats: 13 = DXT1/BC1 (8 bytes per 4x4 block), 15 = DXT5/BC3 (16 bytes).
FMT_BC1 = 13
FMT_BC3 = 15

# TEXTUREFLAGS_ENVMAP. A VTF carrying this bit parses -- header, dimensions,
# format, mip chain all consistent -- and the 2D decoder refuses it, because a
# cubemap's face order is not the decoder's business. That pair (vtf_resolved=1,
# pixels_decoded=0) is the only shape that separates "the texture read" from
# "the texture decoded", and it is the fixture the fake-decode mutation turns
# red: without it, a probe that claimed pixels for every parseable VTF would
# pass every other fixture.
VTF_FLAG_ENVMAP = 0x00004000


def put_string(buf, offset, text):
    raw = text.encode("ascii")
    buf[offset:offset + len(raw)] = raw


def build_mdl(name, cd_materials, texture_names):
    """cd_materials: [dir strings as stored in the MDL]
    texture_names: [mstudiotexture_t names, bare or rooted]"""
    bone_table = HEADER_BYTES
    bone_names = bone_table + BONE_STRIDE  # one bone
    texture_table = bone_names + len("root") + 1
    cd_table = texture_table + TEXTURE_STRIDE * len(texture_names)
    texture_name_strings = cd_table + 4 * len(cd_materials)
    cd_strings = texture_name_strings + sum(len(n) + 1 for n in texture_names)
    total = cd_strings + sum(len(d) + 1 for d in cd_materials)
    buf = bytearray(total)

    struct.pack_into("<III", buf, 0, IDST, MDL_VERSION, 0x0BADF00D)
    put_string(buf, 12, name)
    struct.pack_into("<I", buf, 76, total)          # length, must be <= size
    struct.pack_into("<II", buf, 156, 1, bone_table)
    struct.pack_into("<II", buf, 188, 0, 0)         # sequences
    struct.pack_into("<II", buf, 204, len(texture_names), texture_table)
    struct.pack_into("<II", buf, 212, len(cd_materials), cd_table)
    struct.pack_into("<II", buf, 232, 0, 0)         # bodyparts
    struct.pack_into("<II", buf, 240, 0, 0)         # attachments
    struct.pack_into("<II", buf, 248, 0, 0)         # localnodes

    struct.pack_into("<i", buf, bone_table, bone_names - bone_table)
    struct.pack_into("<i", buf, bone_table + 4, -1)  # parent
    for slot, value in enumerate(IDENTITY34):
        struct.pack_into("<f", buf, bone_table + BONE_POSE_OFFSET + 4 * slot, value)
    put_string(buf, bone_names, "root")

    cursor = texture_name_strings
    for index, texture_name in enumerate(texture_names):
        base = texture_table + index * TEXTURE_STRIDE
        struct.pack_into("<i", buf, base, cursor - base)  # relative name index
        put_string(buf, cursor, texture_name)
        cursor += len(texture_name) + 1

    # numcdtextures entries are ABSOLUTE offsets to the directory strings.
    cursor = cd_strings
    for index, directory in enumerate(cd_materials):
        struct.pack_into("<i", buf, cd_table + index * 4, cursor)
        put_string(buf, cursor, directory)
        cursor += len(directory) + 1

    return bytes(buf)


def build_vtf(width, height, fmt, name, flags=0):
    """One top mip of deterministic non-uniform data. The bytes do not have to
    look like anything; they have to decode, and they have to be reproducible."""
    if fmt == FMT_BC1:
        block_bytes, blocks = 8, ((width + 3) // 4) * ((height + 3) // 4)
    else:
        block_bytes, blocks = 16, ((width + 3) // 4) * ((height + 3) // 4)
    buf = bytearray(80 + block_bytes * blocks)
    buf[0:4] = b"VTF\x00"
    struct.pack_into("<I", buf, 4, 7)              # major
    struct.pack_into("<I", buf, 8, 2)              # minor 7.2: depth at 63, no resource table
    struct.pack_into("<I", buf, 12, 80)            # headerSize
    struct.pack_into("<H", buf, 16, width)
    struct.pack_into("<H", buf, 18, height)
    struct.pack_into("<I", buf, 20, flags)
    struct.pack_into("<H", buf, 24, 1)             # frames
    struct.pack_into("<I", buf, 52, fmt)
    buf[56] = 1                                    # mipCount
    struct.pack_into("<H", buf, 63, 1)             # depth
    seed = sum(ord(c) for c in name)
    for index in range(block_bytes * blocks):
        buf[80 + index] = (index * 7 + seed * 13 + (index >> 8) * 31) & 0xFF
    return bytes(buf)


MATERIAL_DIR = "models/weapons/v_models/vmfix"
OK_STEM = "vmfix_ok"
# The bare stem the wrong-$cdmaterials fixture uses: it exists under the
# correct directory, so a resolver that ignored $cdmaterials and guessed the
# model's own directory would find it. The fixture is the control for that.
SHARED_STEM = "vmfix_ok"

# stem, cdmaterials, texture name, vmt text (None = do not write), vtf (None = do not write)
FIXTURES = (
    # The positive control. BC1, $bumpmap declared, $phong on, no selfillum, no
    # translucent: the feature readings have a known shape. Its VMT/VTF are
    # also the shared material the wrong-$cdmaterials fixture asks for by the
    # same stem under the wrong directory.
    ("vmfix_ok", [MATERIAL_DIR + "/"], SHARED_STEM, ok_vmt(SHARED_STEM),
     build_vtf(64, 64, FMT_BC1, SHARED_STEM)),
    # The VMT is absent. Distinct from the wrong-directory case: the path the
    # resolver should have used is correct, the file simply is not there.
    ("vmfix_no_vmt", [MATERIAL_DIR + "/"], "vmfix_no_vmt", None,
     build_vtf(64, 64, FMT_BC1, "vmfix_no_vmt")),
    # The VMT is present and names this fixture's own texture; the VTF is not.
    ("vmfix_no_vtf", [MATERIAL_DIR + "/"], "vmfix_no_vtf", ok_vmt("vmfix_no_vtf"), None),
    # $cdmaterials points at a directory that does not hold this stem, while
    # the stem itself exists under the correct directory (written by the
    # positive fixture, same bytes). Resolution must fail: a resolver that
    # ignored $cdmaterials and guessed the model's own directory would find it.
    # This fixture writes no VMT/VTF of its own on purpose -- its material is
    # the positive fixture's, and the only thing under test is the directory.
    ("vmfix_bad_cdmaterials", ["models/wrong/dir/"], SHARED_STEM, None, None),
    # The real v_scattergun shape: $basetexture with backslash separators.
    ("vmfix_backslash", [MATERIAL_DIR + "/"], "vmfix_backslash", VMT_BACKSLASH,
     build_vtf(64, 64, FMT_BC1, "vmfix_backslash")),
    # BC3 with selfillum + translucent: the second decode shape, and the
    # has_alpha / has_selfillum readings have somewhere to be true.
    ("vmfix_dxt5", [MATERIAL_DIR + "/"], "vmfix_dxt5", VMT_DXT5,
     build_vtf(64, 64, FMT_BC3, "vmfix_dxt5")),
    # A 4096 side against the 2048 cap. 4096x64 keeps the file at 128 KiB while
    # still carrying a side above the cap; the cap is on the longest side. Its
    # VMT names this fixture's own oversized texture.
    ("vmfix_oversized", [MATERIAL_DIR + "/"], "vmfix_oversized", ok_vmt("vmfix_oversized"),
     build_vtf(4096, 64, FMT_BC1, "vmfix_oversized")),
    # A bare stem with a single empty $cdmaterials entry (the c_pro_rifle
    # shape, except c_pro_rifle's names are rooted). There is no candidate path
    # to try at all, which is its own failure class, not a missing file. The
    # VMT/VTF exist under the correct directory: a resolver that guessed the
    # model's own directory would find them, which is what makes this a
    # statement about the path rule rather than about missing bytes.
    ("vmfix_bare", [""], "vmfix_bare", ok_vmt("vmfix_bare"),
     build_vtf(64, 64, FMT_BC1, "vmfix_bare")),
    # A material whose $basetexture escapes the asset root. resourcePath keeps
    # the materials/ prefix and the ..; the root boundary must refuse it.
    ("vmfix_escape", [MATERIAL_DIR + "/"], "vmfix_escape", VMT_ESCAPE,
     build_vtf(64, 64, FMT_BC1, "vmfix_escape")),
    # A VTF that parses and cannot be decoded as a 2D texture: the env-map flag
    # is set, so the header, dimensions and mip chain are all consistent, and
    # the 2D decoder refuses cubemaps by contract. This is the fixture that
    # separates "the texture was read" from "the texture produced pixels", and
    # the only input that can catch a probe claiming pixels it did not decode.
    ("vmfix_cubemap", [MATERIAL_DIR + "/"], "vmfix_cubemap", ok_vmt("vmfix_cubemap"),
     build_vtf(64, 64, FMT_BC1, "vmfix_cubemap", flags=VTF_FLAG_ENVMAP)),
)

PLACEHOLDERS = (
    ("materials", ".gitkeep"),
    ("maps", ".gitkeep"),
)


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: viewmodel-material-fixture-gen.py <vmfix-root>\n")
        return 2
    root = argv[1]
    model_dir = os.path.join(root, "models", "weapons", "v_models")
    material_dir = os.path.join(root, "materials", *MATERIAL_DIR.split("/"))
    os.makedirs(model_dir, exist_ok=True)
    os.makedirs(material_dir, exist_ok=True)
    for store, name in PLACEHOLDERS:
        store_dir = os.path.join(root, store)
        os.makedirs(store_dir, exist_ok=True)
        with open(os.path.join(store_dir, name), "w", encoding="ascii", newline="\n") as handle:
            handle.write("placeholder so AssetRoot::valid() accepts this fixture root\n")
    for stem, cd_materials, texture_name, vmt_text, vtf_bytes in FIXTURES:
        mdl = build_mdl(stem + ".mdl", cd_materials, [texture_name])
        with open(os.path.join(model_dir, stem + ".mdl"), "wb") as handle:
            handle.write(mdl)
        if vmt_text is not None:
            with open(os.path.join(material_dir, stem + ".vmt"), "w", encoding="ascii", newline="\n") as handle:
                handle.write(vmt_text)
        if vtf_bytes is not None:
            # vmfix_ok's VTF is shared by the wrong-$cdmaterials fixture, which
            # asks for the same stem under the wrong directory; the bytes are
            # written once under the correct name.
            with open(os.path.join(material_dir, stem + ".vtf"), "wb") as handle:
                handle.write(vtf_bytes)
        print("wrote %s/%s.mdl (%d bytes, cdmaterials=%s, vmt=%s, vtf=%s)"
              % (model_dir.replace(os.sep, "/"), stem, len(mdl), cd_materials,
                 "yes" if vmt_text is not None else "no",
                 "yes" if vtf_bytes is not None else "no"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
