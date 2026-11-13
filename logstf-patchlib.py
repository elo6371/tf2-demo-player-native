#!/usr/bin/env python3
"""apply() -- replace a source needle without rewriting the rest of the file.

WHY THIS FILE EXISTS
   logstf-check.sh mutates the product sources by exact text replacement. The
   needles are written with LF, because that is how the sources look in git. The
   WORKING TREE is not necessarily LF: this repository sets core.autocrlf=true,
   so a checkout of the same commit in the integration tree is CRLF while a
   fresh branch is LF.

   The first version of those patches read the file with `newline=""`, which
   preserves CRLF, and then compared it against an LF needle. On 2026-10-23 that
   made five mutations silently inapplicable in the integration tree (m2, m5,
   m6, m9 said "needle not found"; m3 said nothing at all, see below), while the
   same gate was fully green on the LF branch. Reproduced, not inferred: the
   same five needles matched 1/5 against a CRLF copy and 5/5 against an LF one.

   The fix is NOT to normalise the file. Reading with universal newlines and
   writing with newline="\n" also matches on both trees, but it rewrites EVERY
   line of the file as LF: the mutation diff becomes a whole-file rewrite that
   nobody can review, and a restore that goes wrong leaves a source whose only
   visible difference is its line endings. So: read byte-faithfully, re-join the
   NEEDLE with the file's own line ending, replace that, write back byte-
   faithfully. Exactly the named lines change.

AUTHOR'S NOTE ON THE SILENT CASE
   m3 required the needle TWICE and used `sys.exit(3)` on a mismatch. Its bash
   caller had no `else`, so a failed setup printed nothing anywhere: the run's
   only trace of that mutation was its ABSENCE from the verdict list. That is
   the worst possible shape for this failure -- a reader scanning for "FAIL"
   sees a clean log. Callers must have an else branch; this module cannot
   enforce that, but it makes every failure raise, and `apply` returns the site
   count so a caller can print what it did.
"""

import sys


def _eol_of(text):
    """The file's line ending, by majority: the first one found wins."""
    first_crlf = text.find("\r\n")
    first_lf = text.find("\n")
    if first_crlf < 0:
        return "\n"
    if first_lf < 0:
        return "\r\n"
    return "\r\n" if first_crlf <= first_lf else "\n"


def _to_eol(text, eol):
    """Re-join LF text with eol. Existing CRLF is normalised first, so this is
    idempotent and cannot produce \r\r\n."""
    return text.replace("\r\n", "\n").replace("\n", eol)


def apply(path, needle, replacement, expected=1):
    """Replace `needle` with `replacement` in `path`, `expected` times.

    Returns the number of sites replaced. Exits 3 with a message naming the
    actual and expected counts -- never a silent no-op.
    """
    raw = open(path, "r", encoding="utf-8", newline="").read()
    eol = _eol_of(raw)
    want = _to_eol(needle, eol)
    have = raw.count(want)
    if have != expected:
        sys.stderr.write(
            "needle count %d, expected %d in %s (file eol=%r)\n"
            % (have, expected, path, eol))
        sys.exit(3)
    new = raw.replace(want, _to_eol(replacement, eol), expected)
    open(path, "w", encoding="utf-8", newline="").write(new)
    return have
