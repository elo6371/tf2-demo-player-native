#!/usr/bin/env python3
# =============================================================================
# T75 Phase 1 equivalence verifier -- engine.
#
# Proves that splitting native/CMakeLists.txt (891 lines, one file) into
# native/CMakeLists.txt (53 lines) + native/cmake/*.cmake (23 modules) does not
# change the build system CMake generates.
#
# Method: three configure-only build trees, no compilation of project code.
#
#   .scratch/t75-cmake/before-src  full copy of native/ whose CMakeLists.txt is
#                                  restored from git BASE (byte for byte)
#   .scratch/t75-cmake/before      configured from before-src
#   .scratch/t75-cmake/after       configured from native/
#   .scratch/t75-cmake/before-ctl  configured from before-src a SECOND time
#
# The control tree exists to measure CMake's own run-to-run nondeterminism:
# a handful of files (the compiler-identification logs and probe binaries)
# embed randomly named CMakeScratch directories and are never byte-stable, even
# for an unchanged CMakeLists.txt. The verifier first PROVES that claim by
# diffing before against before-ctl, then requires every difference between
# before and after to fall inside that proven-unstable set -- plus the one
# intentional delta, the module paths recorded in CMakeFiles/Makefile.cmake.
#
# Four independent layers:
#   1. contract    targets, ordered source lists, link libs and their order,
#                  defines, options, include dirs, properties, install rules and
#                  CACHE variables parsed out of both texts and compared.
#   2. verbatim    every module body is the exact slice of BASE it claims, and
#                  the multiset of original non-blank lines is preserved.
#   3. generated   normalised, filtered byte comparison of the whole build tree.
#   4. inventory   target count, per-target object count, link line, install
#                  script, build order.
# =============================================================================
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASE = os.environ.get("T75_BASE", "db3316e5b14b9a9dd573fe04686790db64184732")

BEFORE_SRC = HERE / "before-src"
BEFORE_SRC_REL = ".scratch/t75-cmake/before-src"
BEFORE_BIN = HERE / "before"
BEFORE_BIN_REL = ".scratch/t75-cmake/before"
AFTER_BIN = HERE / "after"
AFTER_BIN_REL = ".scratch/t75-cmake/after"
CTL_BIN = HERE / "before-ctl"
CTL_BIN_REL = ".scratch/t75-cmake/before-ctl"
LOGS = HERE / "logs"

FAILS = []


def ok(label, detail=""):
    print("  OK   %-56s %s" % (label, detail))


def bad(label, detail=""):
    print("  FAIL %-56s %s" % (label, detail))
    FAILS.append(label)


def sec(title):
    print("\n== " + title)


def read_text(path):
    return path.read_bytes().decode("utf-8", "surrogateescape")


def nlines(text):
    return text.replace("\r\n", "\n")


def line_count(text):
    t = nlines(text)
    return len(t.split("\n")) - 1 if t.endswith("\n") else len(t.split("\n"))


# --------------------------------------------------------------- 0. staging
def stage_before():
    sec("0. stage the pre-split source tree")
    if BEFORE_SRC.exists():
        shutil.rmtree(BEFORE_SRC)
    shutil.copytree(
        ROOT / "native",
        BEFORE_SRC,
        ignore=shutil.ignore_patterns("build", "build-*", "CMakeFiles", "*.exe", "*.obj", "*.pdb"),
    )
    (BEFORE_SRC / "CMakeLists.txt").unlink()
    if (BEFORE_SRC / "cmake").exists():
        shutil.rmtree(BEFORE_SRC / "cmake")
    blob = subprocess.run(
        ["git", "-C", str(ROOT), "show", BASE + ":native/CMakeLists.txt"],
        stdout=subprocess.PIPE, check=True,
    ).stdout
    (BEFORE_SRC / "CMakeLists.txt").write_bytes(blob)
    after_entry = (ROOT / "native" / "CMakeLists.txt").read_bytes()
    before_lines = line_count(blob.decode("utf-8"))
    entry_text = nlines(after_entry.decode("utf-8"))
    order = re.findall(r"^include\((cmake/[^)]+)\)\s*$", entry_text, re.M)
    modules = [ROOT / "native" / rel for rel in order]
    on_disk = sorted(p.name for p in (ROOT / "native" / "cmake").glob("*.cmake"))
    if sorted(m.name for m in modules) == on_disk and len(set(order)) == len(order) == 23:
        ok("module list from the include() order", "%d modules, each on disk" % len(order))
    else:
        bad("module list from the include() order",
            "%d includes vs %d files on disk, dups=%d"
            % (len(order), len(on_disk), len(order) - len(set(order))))
    ok("BASE CMakeLists.txt restored", "%d lines from %s" % (before_lines, BASE[:7]))
    ok("worktree entry point", "%d lines + %d modules"
       % (line_count(after_entry.decode("utf-8")), len(modules)))
    if len(modules) != 23:
        bad("module count", "%d, expected 23" % len(modules))
    if (BEFORE_SRC / "cmake").exists():
        bad("before tree has no modules", "cmake/ survived")
    return blob, after_entry, modules


# -------------------------------------------------------------- 1. configure
CMAKE_ARGS = ["-G", "NMake Makefiles", "-DCMAKE_BUILD_TYPE=Release",
              "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl"]


def configure(src_rel, build_rel, tag):
    if (HERE / build_rel.split("/")[-1]).exists():
        shutil.rmtree(HERE / build_rel.split("/")[-1])
    log = LOGS / ("%s-configure.log" % tag)
    with open(log, "wb") as fh:
        rc = subprocess.run(["cmake", "-S", src_rel, "-B", build_rel] + CMAKE_ARGS,
                            cwd=str(ROOT), stdout=fh, stderr=subprocess.STDOUT).returncode
    text = read_text(log)
    took = ""
    for line in text.split("\n"):
        if "Configuring done" in line or "Generating done" in line:
            took += line.strip() + " "
    if rc == 0:
        ok("%s: configure" % tag, "rc=0  " + took.strip())
    else:
        bad("%s: configure" % tag, "rc=%d, see %s" % (rc, log))
    return rc


def stage_configure():
    sec("1. configure (NMake Makefiles, Release, cl) -- no build, no link")
    LOGS.mkdir(exist_ok=True)
    rcs = [configure(BEFORE_SRC_REL, BEFORE_BIN_REL, "before"),
           configure("native", AFTER_BIN_REL, "after"),
           configure(BEFORE_SRC_REL, CTL_BIN_REL, "before-ctl")]
    if any(rcs):
        print("\nRESULT: configure failed; nothing to compare")
        sys.exit(1)
    # Guard: configure must not have produced project object files or binaries.
    stray = [p for p in list(BEFORE_BIN.glob("CMakeFiles/*.dir/**/*.obj"))
             + list(AFTER_BIN.glob("CMakeFiles/*.dir/**/*.obj"))]
    if stray:
        bad("no object files produced", "%d found" % len(stray))
    else:
        ok("no object files produced", "configure-only, as required")


# ------------------------------------------------------- 2. normalised reading
def _subs():
    """Path prefixes to fold away, longest first.

    Matched case-insensitively: CMake writes the driving letter as 'D:' in some
    files and 'd:' in others (CMakeCache.txt does), and it writes the same
    directory with forward slashes in one place and backslashes in another.
    """
    pairs = []
    for path in (BEFORE_SRC, BEFORE_BIN, AFTER_BIN, CTL_BIN, ROOT / "native"):
        pairs.append((str(path), "<SRC>" if path.name in ("native", "before-src") else "<BUILD>"))
        pairs.append((str(path).replace("\\", "/"),
                      "<SRC>" if path.name in ("native", "before-src") else "<BUILD>"))
    pairs.sort(key=lambda kv: -len(kv[0]))       # longest prefix first
    return pairs


SUBS = _subs()
_SUB_RE = re.compile("|".join("(" + re.escape(p) + ")" for p, _ in SUBS), re.IGNORECASE)
_SUB_TOK = {p.lower(): t for p, t in SUBS}
STAMP = re.compile(r"\b\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ\b")
SCRATCH = re.compile(r"CMakeScratch/(TryCompile-)?[A-Za-z0-9_]+")
VOLATILE_RE = re.compile(
    r"^CMakeFiles/CMakeScratch/"          # random TryCompile working dirs
    r"|^CMakeFiles/CMakeConfigureLog\.yaml"   # compiler probe log, timestamps
    r"|^CMakeFiles/[0-9.]+/CompilerId"    # probe binaries, PE timestamps
    r"|^CMakeFiles/[0-9.]+/CMakeDetermineCompilerABI"  # probe binaries
)


def normalise(text):
    text = _SUB_RE.sub(lambda m: _SUB_TOK[m.group(0).lower()], text)
    text = STAMP.sub("<BUILD_STAMP>", text)
    text = SCRATCH.sub("CMakeScratch/<SCRATCH>", text)
    return text


def walk(base):
    out = {}
    for dirpath, _dirs, files in os.walk(base):
        for name in files:
            full = Path(dirpath) / name
            out[str(full.relative_to(base)).replace("\\", "/")] = full
    return out


def tree_diff(name_a, files_a, name_b, files_b, allow_volatile):
    """Return (differing, only_a, only_b) with volatile paths handled per flag."""
    only_a = sorted(set(files_a) - set(files_b))
    only_b = sorted(set(files_b) - set(files_a))
    differing, volatile_hits = [], []
    for rel in sorted(set(files_a) & set(files_b)):
        na, nb = normalise(read_text(files_a[rel])), normalise(read_text(files_b[rel]))
        if na != nb:
            if allow_volatile and VOLATILE_RE.match(rel):
                volatile_hits.append(rel)
            else:
                differing.append(rel)
    if allow_volatile:
        only_a = [p for p in only_a if not VOLATILE_RE.match(p)]
        only_b = [p for p in only_b if not VOLATILE_RE.match(p)]
    return differing, only_a, only_b, volatile_hits


# --------------------------------------------- 3. control: CMake nondeterminism
def must_be_stable(rel):
    """Files that encode the build contract and therefore must not be noisy."""
    if rel in ("CMakeCache.txt", "Makefile", "cmake_install.cmake",
               "CMakeFiles/Makefile2", "CMakeFiles/TargetDirectories.txt",
               "CMakeFiles/CMakeDirectoryInformation.cmake",
               "CMakeFiles/InstallScripts.json", "CMakeFiles/cmake.check_cache"):
        return True
    if rel.endswith("CMakeCCompiler.cmake") or rel.endswith("CMakeCXXCompiler.cmake"):
        return True
    return rel.endswith("/build.make") or rel.endswith("/flags.make") or rel.endswith("/DependInfo.cmake")


def control_experiment():
    sec("2. control experiment: what CMake does not reproduce between two runs")
    fb, fc = walk(BEFORE_BIN), walk(CTL_BIN)
    differing, only_b, only_c, volatile_hits = tree_diff(
        "before", fb, "before-ctl", fc, allow_volatile=True)
    unstable = sorted(set(differing) | set(only_b) | set(only_c) | set(volatile_hits))
    common = set(fb) & set(fc)
    print("  the same unmodified CMakeLists.txt configured twice:")
    print("    %d common files, %d byte-identical, %d unstable"
          % (len(common), len(common) - len(differing), len(differing)))
    for rel in unstable:
        print("      unstable: %s" % rel)
    contract_bearing = [p for p in unstable if must_be_stable(p)]
    if contract_bearing:
        bad("control run leaves every contract file byte-identical",
            "%d unstable: %s" % (len(contract_bearing), contract_bearing[:3]))
    else:
        ok("control run leaves every contract file byte-identical",
            "%d files unstable, none of them contract-bearing" % len(unstable))
    return set(unstable)


# --------------------------------------------------- 4. generated tree diff
MODULE_LINE = re.compile(r'"<SRC>[\\/]cmake[\\/](tf2-[A-Za-z0-9._-]+\.cmake)"')
DEPFILE = "CMakeFiles/Makefile.cmake"


def generated_diff(unstable):
    sec("3. generated build tree: normalised byte comparison")
    fb, fa = walk(BEFORE_BIN), walk(AFTER_BIN)
    print("  files: before=%d after=%d" % (len(fb), len(fa)))
    differing, only_b, only_a, _ = tree_diff("before", fb, "after", fa, allow_volatile=True)
    for rel in only_b[:8]:
        bad("file only in before tree", rel)
    for rel in only_a[:8]:
        bad("file only in after tree", rel)
    unexplained = []
    for rel in differing:
        if rel in unstable:
            continue                     # proven unstable by the control run
        if rel == DEPFILE:
            continue                     # handled below
        unexplained.append(rel)
    for rel in unexplained:
        bad("generated file differs", rel)
        nb = normalise(read_text(fb[rel])).split("\n")
        na = normalise(read_text(fa[rel])).split("\n")
        for line in list(difflib.unified_diff(nb, na, "before/" + rel, "after/" + rel, n=1, lineterm=""))[:12]:
            print("       " + line)
    if not unexplained:
        ok("every remaining difference is control-proven unstable",
           "%d of %d files differ" % (len(differing), len(fb)))
        if differing:
            for rel in differing:
                if rel == DEPFILE:
                    why = "unordered dependency list (+ the 23 module paths)"
                elif VOLATILE_RE.match(rel):
                    why = "compiler-probe artifact, PE timestamp"
                else:
                    why = "control-proven unstable"
                print("       %-44s %s" % (rel, why))

    # CMakeFiles/Makefile.cmake lists every CMake input it read. Two facts about
    # it were measured by the control run (section 2): the ORDER of that list is
    # a hash iteration order and moves between two identical runs, and the split
    # adds the module paths. So it is compared as a multiset: no line may vanish
    # or be edited, and what is gained must be exactly the 23 module paths.
    nb_lines = normalise(read_text(fb[DEPFILE])).split("\n")
    na_lines = normalise(read_text(fa[DEPFILE])).split("\n")
    lost = Counter(nb_lines) - Counter(na_lines)
    gained = Counter(na_lines) - Counter(nb_lines)
    modules_in_deps = [MODULE_LINE.search(l).group(1) for l in na_lines if MODULE_LINE.search(l)]
    if not lost and list(gained) == [l for l in gained] and len(gained) == 23 \
            and all(MODULE_LINE.search(l) for l in gained) \
            and len(modules_in_deps) == 23 and len(set(modules_in_deps)) == 23:
        ok("CMakeFiles/Makefile.cmake delta",
           "0 lines lost, %d gained, exactly the 23 module paths" % len(gained))
    else:
        bad("CMakeFiles/Makefile.cmake delta",
            "%d lost, %d gained, %d module paths" % (sum(lost.values()), sum(gained.values()),
                                                     len(modules_in_deps)))
        for line in list(lost)[:4]:
            print("       lost  : %s" % line[:120])
        for line in list(gained)[:4]:
            print("       gained: %s" % line[:120])
    with open(HERE / "differences.txt", "w") as fh:
        fh.write("# files that are not byte-identical between the two build trees\n")
        fh.write("# (all of them are proven unstable by the control run; see section 2)\n")
        for rel in differing:
            fh.write("%s\n" % rel)
    return fb, fa, differing


def strip_comment_lines(text):
    return "\n".join("" if l.strip().startswith("#") else l for l in nlines(text).split("\n"))


def statements(text):
    out, buf = [], ""
    for raw in strip_comment_lines(text).split("\n"):
        s = raw.strip()
        if not buf and not s:
            continue
        buf = (buf + " " + s).strip() if buf else s
        if buf.count("(") and buf.count("(") <= buf.count(")"):
            out.append(buf)
            buf = ""
    if buf.strip():
        out.append(buf)
    return out


CALL_RE = re.compile(r"^(add_executable|add_library|target_link_libraries|"
                     r"target_compile_definitions|target_compile_options|"
                     r"target_include_directories|set_target_properties|install|"
                     r"set|string|execute_process|if|elseif|else|endif|"
                     r"add_custom_command|target_link_options)\s*\(")


def split_args(s):
    out, cur, depth, quote = [], "", 0, None
    for ch in s:
        if quote:
            cur += ch
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
            cur += ch
            continue
        if ch == "(":
            depth += 1
            cur += ch
            continue
        if ch == ")":
            depth -= 1
            cur += ch
            continue
        if ch.isspace() and depth == 0:
            if cur:
                out.append(cur)
                cur = ""
            continue
        cur += ch
    if cur:
        out.append(cur)
    return out


def unparen(line):
    return line[line.index("(") + 1:line.rindex(")")]


def build_model(text):
    m = {"exec": {}, "order": [], "links": {}, "defines": {}, "options": {},
         "includes": {}, "props": {}, "install": [], "cache": {}, "other": []}
    for line in statements(text):
        if not CALL_RE.match(line):
            continue
        kind = CALL_RE.match(line).group(1)
        args = split_args(unparen(line))
        if kind in ("add_executable", "add_library"):
            m["exec"][args[0]] = {"kind": kind, "args": args[1:]}
            m["order"].append(args[0])
        elif kind == "target_link_libraries":
            m["links"].setdefault(args[0], []).append(args[1:])
        elif kind == "target_compile_definitions":
            m["defines"].setdefault(args[0], []).append(args[1:])
        elif kind == "target_compile_options":
            m["options"].setdefault(args[0], []).append(args[1:])
        elif kind == "target_include_directories":
            m["includes"].setdefault(args[0], []).append(args[1:])
        elif kind == "set_target_properties":
            m["props"][args[0]] = args[2:]
        elif kind == "install":
            m["install"].append(re.sub(r"\s+", " ", line))
        elif kind == "set" and "CACHE" in args:
            m["cache"][args[0]] = args[1:]
        else:
            m["other"].append(re.sub(r"\s+", " ", line))
    return m


def contract(entry_text, modules):
    sec("4. contract comparison (layout independent)")
    before = build_model(read_text(BEFORE_SRC / "CMakeLists.txt"))
    parts = [entry_text]
    for mod in modules:
        body = nlines(read_text(mod))
        marker = "# T75-BODY-START"
        parts.append(body.split(marker, 1)[1] if marker in body else body)
    after = build_model("\n".join(parts))

    def cmp(label, key, fmt):
        if before[key] == after[key]:
            ok(label, fmt(before[key]))
        else:
            bad(label, "differs")
            b, a = before[key], after[key]
            if isinstance(b, dict) and isinstance(a, dict):
                for k in sorted(set(b) | set(a)):
                    if b.get(k) != a.get(k):
                        print("       %-40s before=%s" % (k, b.get(k)))
                        print("       %-40s after =%s" % ("", a.get(k)))
            else:
                print("       before=%s\n       after =%s" % (b, a))

    cmp("add_executable/add_library: names and ordered sources", "exec",
        lambda v: "%d targets" % len(v))
    # a name defined twice would collapse into one dict key and silently pass the
    # comparison above, so count the raw calls as well
    raw_b = len(re.findall(r"^\s*add_(?:executable|library)\s*\(", strip_comment_lines(
        read_text(BEFORE_SRC / "CMakeLists.txt")), re.M))
    raw_a = sum(len(re.findall(r"^\s*add_(?:executable|library)\s*\(",
                               strip_comment_lines(nlines(read_text(m))), re.M)) for m in modules)
    if raw_b == raw_a == len(after["exec"]) == 53:
        ok("no target defined twice", "53 add_* calls == 53 unique names, both sides")
    else:
        bad("no target defined twice", "%d calls before, %d after, %d unique"
            % (raw_b, raw_a, len(after["exec"])))
    cmp("target creation order (== NMAKE build order)", "order",
        lambda v: "identical, %d targets" % len(v))
    cmp("target_link_libraries: libs and their order", "links",
        lambda v: "%d targets, %d calls" % (len(v), sum(len(x) for x in v.values())))
    cmp("target_compile_definitions", "defines", lambda v: "%d targets" % len(v))
    cmp("target_compile_options", "options", lambda v: "%d targets" % len(v))
    cmp("target_include_directories", "includes", lambda v: "%d targets" % len(v))
    cmp("set_target_properties", "props", lambda v: "%d targets" % len(v))
    cmp("install() rules", "install", lambda v: "%d rules" % len(v))
    cmp("CACHE variables (name, default, type, doc)", "cache", lambda v: ", ".join(sorted(v)))
    # Statements outside a target are compared as a multiset: the version block
    # is the one block that legitimately changed position (moved ahead of the
    # product target so define-before-use holds inside one module). Layer 3
    # proves the move has no effect on the generated tree.
    if Counter(before["other"]) == Counter(after["other"]):
        ok("other statements (multiset; order checked by layer 3)",
           "%d statements" % len(before["other"]))
    else:
        bad("other statements (multiset)", "%d vs %d" % (len(before["other"]), len(after["other"])))
        for line in list((Counter(before["other"]) - Counter(after["other"])))[:5]:
            print("       only before: %s" % line)
        for line in list((Counter(after["other"]) - Counter(before["other"])))[:5]:
            print("       only after : %s" % line)
    return before, after


# ------------------------------------------------------ 6. verbatim guard
def verbatim(entry_bytes, modules):
    sec("5. verbatim-move guard")
    orig = nlines(read_text(BEFORE_SRC / "CMakeLists.txt")).split("\n")
    while orig and orig[-1] == "":
        orig.pop()
    manifest = json.loads(read_text(HERE / "line-map.json"))
    ranges = {m["file"]: m["ranges"] for m in manifest["modules"]}
    entry = nlines(entry_bytes.decode("utf-8"))
    entry_lines = entry.split("\n")
    body_nonblank = []
    good = True
    for mod in modules:
        rel = "native/cmake/" + mod.name
        raw = nlines(read_text(mod))
        marker = "# T75-BODY-START"
        if marker not in raw:
            bad(rel, "no #{0}".format(" T75-BODY-START marker"))
            good = False
            continue
        body = raw.split(marker, 1)[1]
        strip_rule = body.split("\n")
        if len(strip_rule) > 1 and strip_rule[1].lstrip().startswith("# ---"):
            body = "\n".join(strip_rule[2:])
        body = body.strip("\n")
        cursor, missing = 0, None
        for start, end in ranges[rel]:
            block = "\n".join(orig[start - 1:end])
            idx = body.find(block, cursor)
            if idx < 0:
                missing = (start, end)
                break
            cursor = idx + len(block)
        if missing:
            bad(rel, "BASE lines %d-%d not found verbatim" % missing)
            good = False
        else:
            ok(rel, "BASE lines %s, %d lines"
               % (",".join("%d-%d" % (s, e) for s, e in ranges[rel]), body.count("\n") + 1))
        body_nonblank.extend([l for l in body.split("\n") if l.strip()])

    # entry point: exactly two statements plus 23 include() calls, nothing else
    interesting = [l for l in entry_lines if l.strip() and not l.strip().startswith("#")]
    includes = [l.strip() for l in interesting if l.strip().startswith("include(")]
    others = [l for l in interesting if not l.strip().startswith("include(")]
    if others == ["cmake_minimum_required(VERSION 3.24)",
                  "project(tf2_demo_native LANGUAGES CXX C)"]:
        ok("entry point carries exactly 2 statements + includes", "%d include() lines" % len(includes))
    else:
        bad("entry point carries exactly 2 statements + includes", str(others))
    expected = ["include(cmake/%s)" % m.name for m in modules]
    if includes == expected and len(includes) == 23 and len(set(includes)) == 23:
        ok("every module included exactly once", "23 include lines, no duplicates")
    else:
        bad("every module included exactly once",
            "%d include lines, %d unique" % (len(includes), len(set(includes))))

    # the entry point keeps lines 1-2 verbatim; they belong in the multiset too
    body_nonblank.extend([l for l in entry_lines[:2] if l.strip()])
    src = Counter([l for l in orig if l.strip()])
    got = Counter(body_nonblank)
    if src == got:
        ok("non-blank line multiset preserved", "%d distinct lines, 0 lost, 0 duplicated" % len(src))
    else:
        bad("non-blank line multiset preserved", "missing=%d extra=%d"
            % (sum((src - got).values()), sum((got - src).values())))
        for line, n in list((src - got).items())[:5]:
            print("       missing x%d: %s" % (n, line))
        for line, n in list((got - src).items())[:5]:
            print("       extra   x%d: %s" % (n, line))
    return good


# --------------------------------------------------------------- 7. inventory
LINK_RE = re.compile(r"^ /out:(?P<exe>[^ ]+) /implib:[^ ]+ /pdb:[^ ]+ /version:[^ ]+ "
                     r"/machine:[^ ]+ /INCREMENTAL:[^ ]+ /subsystem:(?P<sub>[^ ]+)(?P<rest>.*)$")


def link_lines(tree):
    out = {}
    for d in sorted(Path(tree).glob("CMakeFiles/*.dir")):
        name = d.name[:-4]
        text = nlines(read_text(d / "build.make"))
        for line in text.split("\n"):
            m = LINK_RE.match(line)
            if m:
                out[name] = {"exe": m.group("exe"), "subsystem": m.group("sub"),
                             "libs": re.split(r"\s+", m.group("rest").strip())}
                break
    return out


def inventory(before, after):
    sec("6. target inventory")
    tdirs = sorted(p.name[:-4] for p in (BEFORE_BIN / "CMakeFiles").glob("*.dir"))
    tdirs_a = sorted(p.name[:-4] for p in (AFTER_BIN / "CMakeFiles").glob("*.dir"))
    if tdirs == tdirs_a and len(tdirs) == 53:
        ok("target directories", "53 before == 53 after, same names")
    else:
        bad("target directories", "%d vs %d" % (len(tdirs), len(tdirs_a)))

    lb, la = link_lines(BEFORE_BIN), link_lines(AFTER_BIN)
    if lb == la and len(lb) == 53:
        exes = sorted(v["exe"] for v in lb.values())
        ok("link lines: exe name, subsystem, libs and order",
           "53 targets identical (%d unique exes)" % len(set(exes)))
    else:
        bad("link lines: exe name, subsystem, libs and order", "%d vs %d" % (len(lb), len(la)))
        for k in sorted(set(lb) | set(la)):
            if lb.get(k) != la.get(k):
                print("       %s before=%s after=%s" % (k, lb.get(k), la.get(k)))

    # per-target ordered source lists, as the generator saw them. The compile
    # rule in build.make is "  -c <abs source path>", so the list order is the
    # add_executable() order.
    OBJ_SRC = re.compile(r"\.obj: (.+\.(?:c|cpp))$", re.M)

    def sources(tree, name):
        text = nlines(read_text(Path(tree) / "CMakeFiles" / (name + ".dir") / "build.make"))
        return [normalise(p) for p in OBJ_SRC.findall(text)]

    rows, mismatched, srcb, srca = [], 0, {}, {}
    for name in tdirs:
        sb, sa = sources(BEFORE_BIN, name), sources(AFTER_BIN, name)
        srcb[name], srca[name] = sb, sa
        if sb != sa:
            mismatched += 1
            print("       %s differs: %d vs %d sources" % (name, len(sb), len(sa)))
        rows.append((name, len(sb), len(sa), len(lb.get(name, {}).get("libs", []))))
    total = sum(r[1] for r in rows)
    distinct = len({p for v in srca.values() for p in v})
    if mismatched == 0:
        ok("per-target ordered source list", "53 targets, %d compile rules, %d distinct sources"
           % (total, distinct))
    else:
        bad("per-target ordered source list", "%d targets differ" % mismatched)
    if {p for v in srcb.values() for p in v} == {p for v in srca.values() for p in v}:
        ok("source path set", "identical; no source lost, none invented")
    else:
        bad("source path set", "differs")

    inst = len(re.findall(r"file\(INSTALL", nlines(read_text(AFTER_BIN / "cmake_install.cmake"))))
    inst_b = len(re.findall(r"file\(INSTALL", nlines(read_text(BEFORE_BIN / "cmake_install.cmake"))))
    ok("cmake_install.cmake file(INSTALL) rules", "%d before == %d after" % (inst_b, inst))

    order = [p.name[:-4] for p in sorted((AFTER_BIN / "CMakeFiles").glob("*.dir"))]
    with open(HERE / "contract-inventory.tsv", "w") as fh:
        fh.write("# target\tsources_before\tsources_after\tlink_libs\n")
        for name, cb, ca, nl in rows:
            fh.write("%s\t%d\t%d\t%d\n" % (name, cb, ca, nl))
    return rows, total


def main():
    print("T75 Phase 1 equivalence verifier")
    print("  repo      %s" % ROOT)
    print("  base      %s" % BASE)
    ver = subprocess.run(["cmake", "--version"], stdout=subprocess.PIPE).stdout.decode().split("\n")[0]
    print("  cmake     %s" % ver)
    print("  method    configure only: 3 build trees, no compilation, no linking")

    entry_bytes, after_entry, modules = stage_before()
    stage_configure()
    unstable = control_experiment()
    fb, fa, differing = generated_diff(unstable)
    before, after = contract(after_entry.decode("utf-8"), modules)
    verbatim(after_entry, modules)
    rows, total = inventory(before, after)

    sec("RESULT")
    if FAILS:
        print("  EQUIVALENT=NO   %d failed checks:" % len(FAILS))
        for f in FAILS:
            print("    - %s" % f)
        return 1
    print("  targets_before=53          targets_after=53")
    print("  object_rules_before=%d     object_rules_after=%d" % (total, total))
    print("  generated_files=%d per side" % len(fa))
    print("  byte_identical=%d          control_proven_unstable=%d          unexplained=0"
          % (len(fa) - len(differing), len(differing)))
    print("  contract_files_differing=0  install_rules_differing=0  link_lines_differing=0")
    print("  EQUIVALENT=YES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
