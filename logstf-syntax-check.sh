#!/usr/bin/env bash
# logstf-syntax-check.sh -- cl /Zs over T54's translation units. NOT a build and
# NOT a gate.
#
# WHY THIS EXISTS
#   T54's sources had never been compiled when they were first queued behind the
#   host lock, so the first real build was a gamble on twenty minutes of lock
#   time. /Zs (syntax check only) reads the same flags the build tree would use,
#   produces no object files, links nothing and writes nothing into the build
#   directory, so it can run while queued.
#
#   It found three real defects in logstf_webview.cpp on its first run -- a file
#   that had never been through a compiler: ICoreWebView2Environment::
#   CreateCoreWebView2Controller called with three arguments instead of two, an
#   extra ", nullptr" after the completed-handler in the environment call (five
#   arguments for a four-argument function), and put_IsVisible called on
#   ICoreWebView2 when the method is on ICoreWebView2Controller. None of those is
#   detectable by reading; all three stop the build.
#
# WHAT IT PROVES, AND WHAT IT DOES NOT
#   It proves the six translation units parse and type-check against the vendored
#   WebView2 header and the product's flags. It does not prove linking, the
#   generated install rules, or any behaviour: the report must say
#   "syntax-verified, not built".
set -uo pipefail
cd "$(dirname "$0")" || exit 90
source ./env-msvc.sh || { echo "SYNTAX=FAIL (no MSVC environment)"; exit 90; }
# MSYS rewrites any argument starting with '/' into a path: the build's
# "/DWIN32 /utf-8" arrived at cl as "C:/Program Files/Git/DWIN32". Two defences:
# tell MSYS not to convert (exported -- a plain assignment is not enough), and
# spell the options with '-', which cl also accepts. Without /utf-8 in particular
# the compiler reads UTF-8 sources as code page 936 and reports phantom
# "newline in constant" errors: a harness bug dressed as a product bug.
export MSYS2_ARG_CONV_EXCL='*'

PROD=native/build-nmake/CMakeFiles/tf2_demo_native.dir/flags.make
PROBE=native/build-nmake/CMakeFiles/logstf_viewer_probe.dir/flags.make
mkdir -p .scratch

check() { # check <label> <flags.make> <source>
  local label="$1" ff="$2" src="$3"
  local def inc fl
  def=$(sed -n 's/^CXX_DEFINES = //p' "$ff" | sed -E 's#(^| )/#\1-#g')
  inc=$(sed -n 's/^CXX_INCLUDES = //p' "$ff" | sed -E 's#(^| )/#\1-#g')
  fl=$(sed -n 's/^CXX_FLAGS = //p' "$ff" | sed -E 's#(^| )/#\1-#g')
  local log=".scratch/syntax-$label.log"
  # shellcheck disable=SC2086
  cl -nologo -Zs -TP $def $inc $fl -c "$src" > "$log" 2>&1
  local rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "  /Zs OK       $label  ($src)"
  else
    echo "  /Zs FAILED   $label  ($src)  rc=$rc"
    grep -E " error |error C|fatal error" "$log" | head -12 | sed 's/^/      /'
  fi
  return "$rc"
}

fail=0
echo "=== /Zs syntax check (no object files, no link) $(date '+%Y-%m-%d %H:%M:%S') ==="
check product-viewer  "$PROD"  native/src/logstf_viewer.cpp            || fail=1
check product-panel   "$PROD"  native/src/logstf_panel.cpp             || fail=1
check product-webview "$PROD"  native/src/logstf_webview.cpp           || fail=1
check product-main    "$PROD"  native/src/main.cpp                     || fail=1
check probe-viewer    "$PROBE" native/src/logstf_viewer.cpp            || fail=1
check probe-tool      "$PROBE" native/tools/logstf_viewer_probe.cpp    || fail=1
if [ "$fail" -eq 0 ]; then
  echo "SYNTAX=PASS (6 translation units, no objects produced, nothing linked)"
else
  echo "SYNTAX=FAIL"
fi
exit "$fail"
