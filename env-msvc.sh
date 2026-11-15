# Sourced by other scripts: puts cl.exe, nmake and the Windows SDK tools (rc.exe,
# mt.exe) on PATH and sets INCLUDE/LIB. vcvars64.bat cannot be used here because
# it calls reg.exe, which this sandbox blocks.
# E3b (2026-11-15): this file is a THIN SHIM over tools/tf2-toolchain.sh, which is
# now the single definition of the MSVC/SDK environment. The four constants that
# used to live here (MSVC_VER 14.44.35207, SDK_VER 10.0.26100.0, and the two root
# paths, one of them this machine's Community edition) were replaced by discovery,
# because this host has TWO Visual Studio installs and a hardcoded root can only
# ever see one of them.
#
# The contract callers rely on is kept, and tightened:
#   * sourcing prints NOTHING on success (all four callers capture stdout);
#   * on failure the environment is left UNCHANGED and the reason is NAMED on
#     stderr by tools/tf2-toolchain.sh, then this file returns 1. The old version
#     exported a half-set environment and let the build die much later with
#     "cl.exe: not found" / CMAKE_MT-NOTFOUND.
# Evidence that the exported environment is byte-identical to HEAD's env-msvc.sh:
# docs/INST1-E3B-TOOLCHAIN-ENTRY-2026-11-15.md and
# D:/_shared/EXEC-2026-11-15/INST1/E3b-entry-out/env-diff.sh.
#
# Residual shell state (NOT environment -- never exported, so child processes do
# not see it): TF2_TOOLCHAIN_RC and TF2_TC_MSVC_ROOT/VER, TF2_TC_SDK_ROOT/VER, the
# toolchain's own diagnostic record. Nothing in the tree reads them.
_tf2_envmsvc_here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_tf2_envmsvc_q="${TF2_TOOLCHAIN_QUIET-}"
TF2_TOOLCHAIN_QUIET=1
if . "$_tf2_envmsvc_here/tools/tf2-toolchain.sh"; then
  if [ -n "$_tf2_envmsvc_q" ]; then TF2_TOOLCHAIN_QUIET="$_tf2_envmsvc_q"; else unset TF2_TOOLCHAIN_QUIET; fi
  unset _tf2_envmsvc_q _tf2_envmsvc_here
else
  printf '%s\n' "env-msvc.sh: FAILED to establish the MSVC/SDK environment (reason above, from tools/tf2-toolchain.sh); the environment was NOT changed" >&2
  if [ -n "$_tf2_envmsvc_q" ]; then TF2_TOOLCHAIN_QUIET="$_tf2_envmsvc_q"; else unset TF2_TOOLCHAIN_QUIET; fi
  unset _tf2_envmsvc_q _tf2_envmsvc_here
  return 1 2>/dev/null || exit 1
fi
