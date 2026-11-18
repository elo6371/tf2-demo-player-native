#!/usr/bin/env bash
# tools/tf2-toolchain.sh -- locate MSVC + the Windows SDK and export PATH/INCLUDE/LIB.
#
# WHY THIS FILE EXISTS
#   env-msvc.sh and build-cmake.sh each carry the same four constants (MSVC_VER,
#   SDK_VER) and the same two root paths, one of which is this machine's VS
#   Community edition. That works on exactly one machine and fails silently
#   everywhere else: this host has TWO Visual Studio installs (BuildTools under
#   "Program Files (x86)", Community under "Program Files"), and the hardcoded
#   path can only ever see one of them.
#
#   This script replaces those constants with three steps, in this order:
#     1. an explicit override (TF2_MSVC_ROOT / TF2_MSVC_VER / TF2_SDK_ROOT /
#        TF2_SDK_VER) so a CI job or another machine needs no edit;
#     2. discovery (vswhere.exe, then a directory scan) when nothing is pinned;
#     3. a hard failure with a named reason when a required tool is missing.
#
#   Step 3 is the point. The old scripts exported a half-set environment and let
#   the build die later with "cl.exe: not found" or CMAKE_MT-NOTFOUND, which is
#   how the compile errors listed in build-cmake.sh:8-18 were discovered.
#
#   vcvars64.bat is still not used: it calls vcvarsall/reg.exe and this sandbox
#   refuses those, so the environment is assembled by hand here.
#
# USAGE
#   source tools/tf2-toolchain.sh     # export the environment into the caller
#   bash tools/tf2-toolchain.sh       # check this machine, print one summary line
#
#   Both forms leave the environment UNCHANGED unless every assertion in step 4
#   passed, and both report failure: sourcing returns 1, executing exits 1.
#
# OVERRIDES
#   TF2_MSVC_ROOT   VS edition root, e.g. /c/Program Files/Microsoft Visual Studio/2022/Community
#   TF2_MSVC_VER    toolset under $TF2_MSVC_ROOT/VC/Tools/MSVC (default: newest)
#   TF2_SDK_ROOT    Windows 10 SDK root (default: /c/Program Files (x86)/Windows Kits/10)
#   TF2_SDK_VER     SDK version under $TF2_SDK_ROOT/Include (default: newest)
#   TF2_NO_VSWHERE  set to any value to skip vswhere and go straight to the scan
#   TF2_TOOLCHAIN_QUIET  set to any value to suppress the summary line

tf2_toolchain_fail() {
  printf 'TF2-TOOLCHAIN: %s\n' "$*" >&2
  TF2_TOOLCHAIN_RC=1
}

# /c/Program Files/... -> C:/Program Files/... . The original env-msvc.sh wrote
# INCLUDE/LIB in exactly this form (drive letter, forward slashes); cl.exe accepts
# it, and the /c/ form is only what PATH needs.
#
# cygpath -m does this properly. It matters: a hand-rolled "cut -c2 -c4" mapping
# silently turns /tmp/... (an MSYS mount, no drive letter) into "T:/p/..." and
# then reports the directory as missing, which is a false negative that reads
# exactly like a real toolchain problem.
tf2_toolchain_to_win() {
  local p="$1" drive rest
  case "$p" in
    /[A-Za-z]/*)
      if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$p"
        return 0
      fi
      drive="$(printf '%s' "$p" | cut -c2-2 | tr 'a-z' 'A-Z')"
      rest="$(printf '%s' "$p" | cut -c4-)"
      printf '%s:/%s' "$drive" "$rest"
      ;;
    *)
      # Not a drive-letter path. cl.exe cannot use this form; say so instead of
      # inventing a drive letter.
      printf '%s' "$p"
      ;;
  esac
}

tf2_toolchain_locate() {
  local msvc_root="${TF2_MSVC_ROOT:-}" msvc_ver="${TF2_MSVC_VER:-}"
  local sdk_root="${TF2_SDK_ROOT:-/c/Program Files (x86)/Windows Kits/10}" sdk_ver="${TF2_SDK_VER:-}"
  local vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
  local p drive base year ed

  # 1. vswhere: the supported way to ask "where is VS installed". It works here
  #    even though vcvars64.bat does not, which is why the one prototype that
  #    preceded this script (scratch/n22/env-locate.sh, not a repo script) used it.
  # The tr -d below is DELIBERATE, not debris. It strips CR (two characters in
  # the source: backslash + r). vswhere.exe answers with a trailing CRLF, and a
  # stray CR inside msvc_root invalidates every path derived from it, so
  # "$msvc_root/VC/Tools/MSVC" would never match and the discovery would fail
  # with a reason that points at the wrong thing.
  #
  # Do NOT replace the escape with a raw 0x0D byte. A literal CR in a shell
  # script is CRLF damage; tools/audit-tree.sh fails the whole tree with
  # "VERDICT: CRLF-DAMAGE" and "tools/tf2-toolchain.sh has 1 CR". That is
  # exactly how this file once failed the commit gate -- the byte was an
  # accident, the escape is the fix, and the escape is the thing to keep.
  if [ -z "$msvc_root" ] && [ -z "${TF2_NO_VSWHERE:-}" ] && [ -x "$vswhere" ]; then
    p="$("$vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>/dev/null | tr -d '\r')"
    if [ -n "$p" ]; then
      # vswhere answers in Windows form ("C:\Program Files\..."). cygpath does the
      # conversion when it exists (MSYS and Git Bash both ship it); the fallback below
      # is the same thing without it. Both MUST drop the separator that follows the
      # drive letter: keeping it produces "/c//Program Files", which Windows tolerates
      # for PATH but which then leaks a "C://" into INCLUDE and LIB.
      if command -v cygpath >/dev/null 2>&1; then
        msvc_root="$(cygpath -u "$p" 2>/dev/null)"
      else
        drive="$(printf '%s' "$p" | cut -c1-1 | tr 'A-Z' 'a-z')"
        msvc_root="/$drive/$(printf '%s' "$p" | cut -c4- | tr "$(printf '\134')" '/')"
      fi
      [ -d "$msvc_root/VC/Tools/MSVC" ] || msvc_root=""
    fi
  fi

  # 2. Scan. Covers a VS on another drive, a Build Tools-only install, and the
  #    TF2_NO_VSWHERE case (that override exists so the fallback stays testable).
  if [ -z "$msvc_root" ]; then
    for base in "/c/Program Files/Microsoft Visual Studio" "/c/Program Files (x86)/Microsoft Visual Studio"; do
      [ -d "$base" ] || continue
      for year in "$base"/*; do
        [ -d "$year" ] || continue
        for ed in "$year"/*; do
          if [ -d "$ed/VC/Tools/MSVC" ]; then msvc_root="$ed"; break 3; fi
        done
      done
    done
  fi

  if [ -z "$msvc_root" ]; then
    tf2_toolchain_fail "no Visual Studio with VC/Tools/MSVC found (vswhere said nothing and the Program Files scan found nothing); set TF2_MSVC_ROOT"
    return 1
  fi
  if [ ! -d "$msvc_root/VC/Tools/MSVC" ]; then
    tf2_toolchain_fail "TF2_MSVC_ROOT=$msvc_root has no VC/Tools/MSVC"
    return 1
  fi

  # 3. Newest toolset / SDK unless pinned. sort -V so a future 14.9 sorts below
  #    14.44 rather than above it.
  if [ -z "$msvc_ver" ]; then
    msvc_ver="$(ls -1 "$msvc_root/VC/Tools/MSVC" 2>/dev/null | grep -E '^[0-9]+[.]' | sort -V | tail -1)"
  fi
  if [ -z "$msvc_ver" ]; then
    tf2_toolchain_fail "no toolset version under $msvc_root/VC/Tools/MSVC"
    return 1
  fi
  if [ -z "$sdk_ver" ]; then
    sdk_ver="$(ls -1 "$sdk_root/Include" 2>/dev/null | grep -E '^[0-9]+[.]' | sort -V | tail -1)"
  fi
  if [ -z "$sdk_ver" ]; then
    tf2_toolchain_fail "no SDK version under $sdk_root/Include; set TF2_SDK_ROOT"
    return 1
  fi

  local msvc_bin="$msvc_root/VC/Tools/MSVC/$msvc_ver/bin/Hostx64/x64"
  local sdk_bin="$sdk_root/bin/$sdk_ver/x64"
  local msvc_w sdk_w
  msvc_w="$(tf2_toolchain_to_win "$msvc_root/VC/Tools/MSVC/$msvc_ver")"
  sdk_w="$(tf2_toolchain_to_win "$sdk_root")"

  # 4. Assert BEFORE exporting. A caller must never inherit half an environment:
  #    that is what made the two build-cmake.sh bugs expensive to find.
  local rc=0 d
  [ -d "$msvc_bin" ] || { tf2_toolchain_fail "toolset bin missing: $msvc_bin (toolset $msvc_ver)"; rc=1; }
  [ -d "$sdk_bin" ] || { tf2_toolchain_fail "SDK bin missing: $sdk_bin (SDK $sdk_ver)"; rc=1; }
  [ -d "$msvc_w/include" ] || { tf2_toolchain_fail "MSVC include missing: $msvc_w/include"; rc=1; }
  for d in ucrt shared um; do
    [ -d "$sdk_w/Include/$sdk_ver/$d" ] || { tf2_toolchain_fail "SDK $d include missing: $sdk_w/Include/$sdk_ver/$d"; rc=1; }
  done
  [ -d "$msvc_w/lib/x64" ] || { tf2_toolchain_fail "MSVC x64 lib missing: $msvc_w/lib/x64"; rc=1; }
  [ -d "$sdk_w/Lib/$sdk_ver/ucrt/x64" ] || { tf2_toolchain_fail "SDK ucrt x64 lib missing: $sdk_w/Lib/$sdk_ver/ucrt/x64"; rc=1; }
  if [ "$rc" -ne 0 ]; then TF2_TOOLCHAIN_RC=1; return 1; fi

  export PATH="$msvc_bin:$sdk_bin:$PATH"
  export INCLUDE="$msvc_w/include;$sdk_w/Include/$sdk_ver/ucrt;$sdk_w/Include/$sdk_ver/shared;$sdk_w/Include/$sdk_ver/um;$sdk_w/Include/$sdk_ver/winrt"
  export LIB="$msvc_w/lib/x64;$sdk_w/Lib/$sdk_ver/ucrt/x64;$sdk_w/Lib/$sdk_ver/um/x64"

  # 5. The tools the build actually invokes. cmake needs cl and nmake; rc and mt
  #    are the two whose absence CMake reports as CMAKE_MT-NOTFOUND and which
  #    make the link step die with "no such file or directory".
  local t
  for t in cl.exe nmake.exe rc.exe mt.exe; do
    if ! command -v "$t" >/dev/null 2>&1; then
      tf2_toolchain_fail "$t is not on PATH after adding $msvc_bin and $sdk_bin"
      rc=1
    fi
  done
  if [ "$rc" -ne 0 ]; then TF2_TOOLCHAIN_RC=1; return 1; fi

  TF2_TC_MSVC_ROOT="$msvc_root"
  TF2_TC_MSVC_VER="$msvc_ver"
  TF2_TC_SDK_ROOT="$sdk_root"
  TF2_TC_SDK_VER="$sdk_ver"
  TF2_TOOLCHAIN_RC=0
  if [ -z "${TF2_TOOLCHAIN_QUIET:-}" ]; then
    printf 'TF2-TOOLCHAIN: vsroot=%s msvc=%s sdk=%s cl=%s rc_exe=%s status=OK\n' \
      "$msvc_root" "$msvc_ver" "$sdk_ver" "$(command -v cl.exe)" "$(command -v rc.exe)"
  fi
  return 0
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  TF2_TOOLCHAIN_RC=1
  tf2_toolchain_locate
  exit "$TF2_TOOLCHAIN_RC"
else
  TF2_TOOLCHAIN_RC=1
  tf2_toolchain_locate
fi

