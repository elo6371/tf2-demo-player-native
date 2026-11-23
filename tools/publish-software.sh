#!/usr/bin/env bash
# tools/publish-software.sh -- publish the SOFTWARE ONLY to the public repository.
#
# The public repository (origin) carries the player and what it takes to build
# it, nothing else: no demos, no evidence, no gate scripts, no docs, no test data
# (owner's decision, 2026-11-22). The development branches are never pushed.
#
#   1. selects the files of a COMMITTED revision (default HEAD) with the rules in
#      publish/manifest.txt -- uncommitted work is never published;
#   2. exports them into .scratch/publish/tree and applies the overlays
#      (public README, .gitignore, .gitattributes);
#   3. refuses the export if it holds a demo, game content, a capture, an archive,
#      a file over 2 MB (one documented exception: the vendored WebView2 header,
#      which is the whole COM API in one file and is required to build), a
#      credential-shaped string, or a forbidden directory;
#   4. --verify-build: builds the export on its own in a throw-away repository;
#   5. makes ONE commit in a separate clone of origin (.scratch/publish/clone),
#      on top of origin/main (default) or as a new root (--fresh-history);
#   6. --push: pushes it to origin main;
#   7. --prune-branches / --prune-tags: deletes every other public branch / every
#      public tag, after main has been pushed.
#
# Irreversible steps are guarded:
#   * --fresh-history + --push needs --yes-rewrite-public-history AND
#     --verify-build in the same run;
#   * --prune-branches needs --yes-delete-remote-branches, --prune-tags needs
#     --yes-delete-remote-tags;
#   * any of the three needs a backup of the WHOLE public repository taken in the
#     same run (--backup-remote is implied), written OUTSIDE this work tree:
#     a mirror clone plus a verified git bundle under $PUBLISH_BACKUP_ROOT
#     (default: ../_shared/public-repo-backup).
#   * the remote's default branch is never deleted; if it is not main, pruning
#     stops and says how to change it.
#
# Usage (Git Bash):
#   bash tools/publish-software.sh --remote-report            # default branch, branches, tags (read-only)
#   bash tools/publish-software.sh --backup-remote            # mirror + bundle of origin, nothing else
#   bash tools/publish-software.sh --verify-build             # dry run: export, checks, build, local commit
#   bash tools/publish-software.sh --fresh-history --verify-build --push \
#        --yes-rewrite-public-history \
#        --prune-branches --yes-delete-remote-branches       # the 2026-11-22 decision
#
# What this cannot reach: GitHub keeps pull-request refs (refs/pull/*) and may
# serve an old commit by its SHA until it is garbage-collected; forks and clones
# other people made keep everything. Removing those needs GitHub Support.
#
# Exit: 0 ok, 1 a check failed, 2 usage/environment error.
set -uo pipefail
# W1 (2026-11-23): every product launch in this script runs at 1280x720, the size
# its pixel pins were measured at. The product reads this when --window-size is
# absent (an explicit --window-size still wins); before W1 1280x720 was simply the
# default, and W1 moves the default to 1920x1080. tools/window-pin-check.sh checks it.
export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
cd "$(dirname "$0")/.." || exit 2
REPO=$PWD

REF=HEAD
PUSH=0
FRESH=0
YES_REWRITE=0
VERIFY_BUILD=0
REPORT_ONLY=0
BACKUP=0
PRUNE_BRANCHES=0
YES_BRANCHES=0
PRUNE_TAGS=0
YES_TAGS=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --ref) REF="${2:?--ref needs a revision}"; shift ;;
    --push) PUSH=1 ;;
    --fresh-history) FRESH=1 ;;
    --yes-rewrite-public-history) YES_REWRITE=1 ;;
    --verify-build) VERIFY_BUILD=1 ;;
    --remote-report|--list-remote-branches) REPORT_ONLY=1 ;;
    --backup-remote) BACKUP=1 ;;
    --prune-branches) PRUNE_BRANCHES=1 ;;
    --yes-delete-remote-branches) YES_BRANCHES=1 ;;
    --prune-tags) PRUNE_TAGS=1 ;;
    --yes-delete-remote-tags) YES_TAGS=1 ;;
    -h|--help) sed -n '2,46p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

MANIFEST=publish/manifest.txt
WORK="$REPO/.scratch/publish"
TREE="$WORK/tree"
CLONE="$WORK/clone"
LIST="$WORK/files.txt"
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP_ROOT="${PUBLISH_BACKUP_ROOT:-$REPO/../_shared/public-repo-backup}"
REMOTE_URL=$(git remote get-url origin 2>/dev/null) || { echo "PUBLISH=FAIL (no origin remote)"; exit 2; }
mkdir -p "$WORK"

# ---- guards ---------------------------------------------------------------------
if [ "$FRESH" -eq 1 ] && [ "$PUSH" -eq 1 ]; then
  [ "$YES_REWRITE" -eq 1 ] || { echo "PUBLISH=REFUSED (--fresh-history --push rewrites main's public history; add --yes-rewrite-public-history)"; exit 2; }
  [ "$VERIFY_BUILD" -eq 1 ] || { echo "PUBLISH=REFUSED (rewriting history requires --verify-build in the same run)"; exit 2; }
fi
if [ "$PRUNE_BRANCHES" -eq 1 ]; then
  [ "$YES_BRANCHES" -eq 1 ] || { echo "PUBLISH=REFUSED (--prune-branches needs --yes-delete-remote-branches)"; exit 2; }
  [ "$PUSH" -eq 1 ] || { echo "PUBLISH=REFUSED (--prune-branches only runs after main is pushed in the same run: add --push)"; exit 2; }
fi
if [ "$PRUNE_TAGS" -eq 1 ]; then
  [ "$YES_TAGS" -eq 1 ] || { echo "PUBLISH=REFUSED (--prune-tags needs --yes-delete-remote-tags)"; exit 2; }
  [ "$PUSH" -eq 1 ] || { echo "PUBLISH=REFUSED (--prune-tags only runs after main is pushed in the same run: add --push)"; exit 2; }
fi
if { [ "$FRESH" -eq 1 ] && [ "$PUSH" -eq 1 ]; } || [ "$PRUNE_BRANCHES" -eq 1 ] || [ "$PRUNE_TAGS" -eq 1 ]; then
  BACKUP=1
fi

# ---- remote report (always) -------------------------------------------------------
git ls-remote --symref origin HEAD > "$WORK/remote-head.txt" 2>/dev/null || { echo "PUBLISH=FAIL (cannot reach origin)"; exit 2; }
DEFAULT_BRANCH=$(sed -n 's#^ref: refs/heads/\([^[:space:]]*\)[[:space:]]*HEAD$#\1#p' "$WORK/remote-head.txt" | head -1)
git ls-remote --heads origin | sed 's#.*refs/heads/##' > "$WORK/remote-branches.txt"
git ls-remote --tags --refs origin | sed 's#.*refs/tags/##' > "$WORK/remote-tags.txt"
echo "=== origin: $REMOTE_URL ==="
echo "default branch: ${DEFAULT_BRANCH:-<unknown>}"
echo "branches ($(wc -l < "$WORK/remote-branches.txt" | tr -d ' ')):"
sed 's/^/  /' "$WORK/remote-branches.txt"
echo "tags ($(wc -l < "$WORK/remote-tags.txt" | tr -d ' ')):"
sed 's/^/  /' "$WORK/remote-tags.txt"
if [ "$REPORT_ONLY" -eq 1 ] && [ "$BACKUP" -eq 0 ]; then exit 0; fi

# ---- backup of the whole public repository -----------------------------------------
if [ "$BACKUP" -eq 1 ]; then
  mkdir -p "$BACKUP_ROOT" || { echo "PUBLISH=FAIL (cannot create $BACKUP_ROOT)"; exit 2; }
  MIRROR="$BACKUP_ROOT/mirror-$STAMP.git"
  BUNDLE="$BACKUP_ROOT/public-$STAMP.bundle"
  echo "=== backup -> $BACKUP_ROOT ==="
  git clone -q --mirror "$REMOTE_URL" "$MIRROR" || { echo "PUBLISH=FAIL (mirror clone)"; exit 1; }
  git -C "$MIRROR" for-each-ref --format='%(objectname) %(refname)' > "$BACKUP_ROOT/refs-$STAMP.txt"
  git -C "$MIRROR" bundle create "$BUNDLE" --all > /dev/null 2>&1 || { echo "PUBLISH=FAIL (bundle create)"; exit 1; }
  git bundle verify "$BUNDLE" > "$BACKUP_ROOT/bundle-verify-$STAMP.txt" 2>&1 || { echo "PUBLISH=FAIL (bundle verify; see $BACKUP_ROOT/bundle-verify-$STAMP.txt)"; exit 1; }
  echo "backup: refs=$(wc -l < "$BACKUP_ROOT/refs-$STAMP.txt" | tr -d ' ') bundle=$(wc -c < "$BUNDLE" | tr -d ' ') bytes, verified"
  echo "restore any branch later with: git fetch \"$BUNDLE\" refs/heads/<name>:refs/heads/<name>"
fi
if [ "$REPORT_ONLY" -eq 1 ]; then exit 0; fi
# --backup-remote on its own: report + backup, nothing else.
if [ "$BACKUP" -eq 1 ] && [ "$PUSH" -eq 0 ] && [ "$VERIFY_BUILD" -eq 0 ] && [ "$FRESH" -eq 0 ]; then
  exit 0
fi

SHA=$(git rev-parse --verify "$REF^{commit}" 2>/dev/null) || { echo "PUBLISH=FAIL (no commit $REF)"; exit 2; }
git cat-file -e "$SHA:$MANIFEST" 2>/dev/null || {
  echo "PUBLISH=FAIL ($MANIFEST is not committed at $REF; commit it first)"; exit 2; }
echo "=== publish software from $(git rev-parse --short "$SHA") ($REF) ==="

# ---- 1. select ----------------------------------------------------------------
RULES=()
OVERLAYS=()
# Inputs go through real files, not <(...): see docs/GATE-FIXES-2026-11-22.md.
git cat-file blob "$SHA:$MANIFEST" > "$WORK/manifest.txt" || { echo "PUBLISH=FAIL (cannot read manifest)"; exit 2; }
while IFS= read -r line || [ -n "$line" ]; do
  line=${line%$'\r'}
  case "$line" in ''|'#'*) continue ;; esac
  case "$line" in
    include\ *|exclude\ *) RULES+=("$line") ;;
    overlay\ *) OVERLAYS+=("${line#overlay }") ;;
    *) echo "PUBLISH=FAIL (bad manifest line: $line)"; exit 2 ;;
  esac
done < "$WORK/manifest.txt"

git ls-tree -r --name-only "$SHA" > "$WORK/all-paths.txt" || { echo "PUBLISH=FAIL (ls-tree)"; exit 2; }
: > "$LIST"
while IFS= read -r path; do
  decision=0
  for rule in "${RULES[@]}"; do
    pattern=${rule#* }
    # shellcheck disable=SC2053 -- the pattern is meant to glob
    if [[ "$path" == $pattern ]]; then
      case "$rule" in include\ *) decision=1 ;; exclude\ *) decision=0 ;; esac
    fi
  done
  [ "$decision" -eq 1 ] && printf '%s\n' "$path" >> "$LIST"
done < "$WORK/all-paths.txt"
selected=$(wc -l < "$LIST" | tr -d ' ')
[ "$selected" -gt 0 ] || { echo "PUBLISH=FAIL (manifest selected nothing)"; exit 1; }

# ---- 2. export ----------------------------------------------------------------
rm -rf "$TREE"
mkdir -p "$TREE"
while IFS= read -r path; do
  mkdir -p "$TREE/$(dirname "$path")"
  git cat-file blob "$SHA:$path" > "$TREE/$path" || { echo "PUBLISH=FAIL (cannot export $path)"; exit 1; }
done < "$LIST"
for overlay in "${OVERLAYS[@]}"; do
  read -r src dst <<< "$overlay"
  git cat-file -e "$SHA:$src" 2>/dev/null || { echo "PUBLISH=FAIL (overlay $src is not committed at $REF)"; exit 1; }
  mkdir -p "$TREE/$(dirname "$dst")"
  git cat-file blob "$SHA:$src" > "$TREE/$dst"
done
echo "selected=$selected overlays=${#OVERLAYS[@]}"
echo "--- by top-level directory ---"
( cd "$TREE" && find . -type f | sed 's#^\./##' | awk -F/ '{ print (NF > 1 ? $1 "/" : $1) }' | sort | uniq -c )

# ---- 3. checks -----------------------------------------------------------------
fail=0
bad() { echo "  FAIL $*"; fail=1; }
# The 2 MB rule exists to keep game content and captures out of the public tree.
# A vendored SDK header is neither: it is source the build needs, and it is named
# in full here so the rule stays "nothing over 2 MB" for every other file. The one
# entry below is the WebView2 COM API in a single header (2.55 MB), which
# logstf_webview.cpp compiles against; it has been in the public tree since
# snapshot-2026-11-18, so excluding it would break the export's build without
# hiding anything.
oversizeExempt() {
  case "$1" in
    native/third_party/webview2/include/WebView2.h) return 0 ;;
  esac
  return 1
}
cd "$TREE" || exit 2
find . -type f > "$WORK/tree-files.txt"
while IFS= read -r f; do
  f=${f#./}
  lower=$(printf '%s' "$f" | tr 'A-Z' 'a-z')
  case "$lower" in
    *.dem|*.bsp|*.vpk|*.mdl|*.vtx|*.vvd|*.phy|*.vtf|*.vmt|*.pcf|*.wav|*.mp3|*.ogg|*.bmp|*.png|*.jpg|*.jpeg|*.pgm|*.trace|*.zip|*.7z|*.rar)
      bad "game content / capture / archive: $f" ;;
  esac
  case "$f" in
    evidence/*|testdata/*|docs/*|analysis/*|scripts/*|.scratch/*|native/tests/*|.github/*)
      bad "forbidden directory: $f" ;;
  esac
  size=$(wc -c < "$f" | tr -d ' ')
  if [ "$size" -gt 2097152 ]; then
    if oversizeExempt "$f"; then
      echo "  note: over 2 MB but exempt (vendored SDK header, required to build): $f ($size bytes)"
    else
      bad "larger than 2 MB ($size bytes): $f"
    fi
  fi
done < "$WORK/tree-files.txt"
if grep -rIlE -- '-----BEGIN [A-Z ]*PRIVATE KEY-----|ghp_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AKIA[0-9A-Z]{16}' . > "$WORK/secrets.txt" 2>/dev/null; then
  while IFS= read -r f; do bad "credential-shaped string in $f"; done < "$WORK/secrets.txt"
fi
for need in LICENSE README.md THIRD-PARTY-NOTICES.md native/CMakeLists.txt build-cmake.sh tools/tf2-toolchain.sh; do
  [ -f "$need" ] || bad "missing required file: $need"
done
# Not a failure: developer paths in comments. Listed so they can be cleaned up.
grep -rIn -E '[A-Za-z]:[/\\](Users[/\\][^/\\ "]+|TF2_Native_Worktrees|TF2_Demo_Player|_shared)' . > "$WORK/local-paths.txt" 2>/dev/null
echo "note: $(wc -l < "$WORK/local-paths.txt" | tr -d ' ') line(s) mention a local developer path (see $WORK/local-paths.txt)"
cd "$REPO" || exit 2
if [ "$fail" -ne 0 ]; then echo "PUBLISH=FAIL (checks)"; exit 1; fi
echo "checks: PASS"

# ---- 4. optional build of the export -------------------------------------------
if [ "$VERIFY_BUILD" -eq 1 ]; then
  BUILD_DIR="$WORK/buildcheck"
  rm -rf "$BUILD_DIR"
  cp -r "$TREE" "$BUILD_DIR"
  (
    cd "$BUILD_DIR" || exit 2
    # The build stamp reads the git index, so the export must be a repository.
    git init -q && git add -A && git -c user.name=publish -c user.email=publish@local commit -q -m export
    bash build-cmake.sh > "$WORK/buildcheck.log" 2>&1
  )
  brc=$?
  grep -aE 'cmake_build_rc=|^errors=|^warnings=|^exe_count=' "$WORK/buildcheck.log" | tail -4
  if [ "$brc" -ne 0 ] || [ ! -f "$BUILD_DIR/native/build-nmake/tf2_demo_native.exe" ]; then
    echo "PUBLISH=FAIL (the exported tree does not build; see $WORK/buildcheck.log)"; exit 1
  fi
  echo "verify-build: PASS (tf2_demo_native.exe built from the export alone)"
fi

# ---- 5. commit in the publish clone -------------------------------------------
# The clone is only the base for one commit on main, so it is fetched SHALLOW and from
# main alone: a full clone of this remote is ~170 MB of history for five branches, and on
# 2026-11-23 two runs died at "fetch-pack: unexpected disconnect ... early EOF" after ~55
# minutes, having transferred nothing that the commit below needed. Depth 1 still fetches
# main's whole tree (which the checkout below needs) and the push is a plain fast-forward
# whose parent the remote already has. PUBLISH_CLONE_DEPTH=0 restores the full clone.
if [ ! -d "$CLONE/.git" ]; then
  CLONE_DEPTH="${PUBLISH_CLONE_DEPTH:-1}"
  if [ "$CLONE_DEPTH" = "0" ]; then
    git clone -q --no-checkout "$REMOTE_URL" "$CLONE" || { echo "PUBLISH=FAIL (clone)"; exit 2; }
  else
    git clone -q --no-checkout --depth "$CLONE_DEPTH" --single-branch --branch main \
      "$REMOTE_URL" "$CLONE" || { echo "PUBLISH=FAIL (clone)"; exit 2; }
  fi
fi
git -C "$CLONE" fetch -q --prune origin || { echo "PUBLISH=FAIL (fetch)"; exit 2; }
REMOTE_MAIN=$(git -C "$CLONE" rev-parse --verify -q origin/main || true)
# Leave any previous publish-tmp so it can be recreated.
git -C "$CLONE" checkout -q --detach 2>/dev/null || true
git -C "$CLONE" branch -D publish-tmp > /dev/null 2>&1 || true
if [ "$FRESH" -eq 1 ]; then
  git -C "$CLONE" checkout -q --orphan publish-tmp
  git -C "$CLONE" rm -rq --cached . > /dev/null 2>&1 || true
else
  [ -n "$REMOTE_MAIN" ] || { echo "PUBLISH=FAIL (origin has no main; use --fresh-history)"; exit 2; }
  git -C "$CLONE" checkout -q -B publish-tmp origin/main
  git -C "$CLONE" rm -rq . > /dev/null 2>&1 || true
fi
# Empty the work tree (except .git), then lay the export down.
find "$CLONE" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -r "$TREE"/. "$CLONE"/
git -C "$CLONE" add -A
if [ "$FRESH" -eq 0 ] && git -C "$CLONE" diff --cached --quiet; then
  echo "nothing to publish: origin/main already equals the software export"
else
  echo "--- change against origin/main ---"
  git -C "$CLONE" diff --cached --shortstat
  git -C "$CLONE" -c core.autocrlf=false commit -q -F - <<MSG
Publish software from $(git rev-parse --short=12 "$SHA")

Software only: the player's sources, build definitions, the build scripts,
LICENSE (GPL-3.0-only) and THIRD-PARTY-NOTICES.md. Selected by
publish/manifest.txt of the development tree; no demos, game content,
evidence, gate scripts or development documents.
MSG
  echo "local publish commit: $(git -C "$CLONE" log --oneline -1)"
fi
git -C "$CLONE" ls-files > "$WORK/published-files.txt"
echo "published file count: $(wc -l < "$WORK/published-files.txt" | tr -d ' ') (list: $WORK/published-files.txt)"

# ---- 6. push main ---------------------------------------------------------------
if [ "$PUSH" -eq 0 ]; then
  echo "NOT PUSHED. Inspect $CLONE, then re-run with --push."
  exit 0
fi
if [ "$FRESH" -eq 1 ]; then
  if [ -n "$REMOTE_MAIN" ]; then
    git -C "$CLONE" push --force-with-lease="main:$REMOTE_MAIN" origin publish-tmp:main \
      || { echo "PUBLISH=FAIL (force push refused -- branch protection on main? see GitHub Settings > Branches)"; exit 1; }
  else
    git -C "$CLONE" push origin publish-tmp:main || exit 1
  fi
else
  git -C "$CLONE" push origin publish-tmp:main || exit 1
fi
echo "PUBLISH=PASS $(git -C "$CLONE" rev-parse --short HEAD) -> origin main"

# ---- 7. prune other branches / tags ---------------------------------------------
prune_rc=0
if [ "$PRUNE_BRANCHES" -eq 1 ]; then
  if [ -n "$DEFAULT_BRANCH" ] && [ "$DEFAULT_BRANCH" != "main" ]; then
    echo "PRUNE-BRANCHES=STOPPED (the default branch is '$DEFAULT_BRANCH', not main; GitHub refuses to delete it)"
    echo "  set the default to main: GitHub > Settings > General > Default branch, or: gh repo edit --default-branch main"
    echo "  then re-run with --prune-branches"
    prune_rc=1
  else
    while IFS= read -r branch; do
      [ -n "$branch" ] && [ "$branch" != "main" ] || continue
      if git push -q origin --delete "refs/heads/$branch"; then
        echo "  deleted branch $branch"
      else
        echo "  FAIL could not delete branch $branch (protected?)"; prune_rc=1
      fi
    done < "$WORK/remote-branches.txt"
  fi
fi
if [ "$PRUNE_TAGS" -eq 1 ]; then
  while IFS= read -r tag; do
    [ -n "$tag" ] || continue
    if git push -q origin --delete "refs/tags/$tag"; then
      echo "  deleted tag $tag"
    else
      echo "  FAIL could not delete tag $tag"; prune_rc=1
    fi
  done < "$WORK/remote-tags.txt"
fi
echo "=== origin after publish ==="
git ls-remote --heads --tags --refs origin | sed 's#.*refs/##; s/^/  /'
[ "$prune_rc" -eq 0 ] || exit 1
exit 0
