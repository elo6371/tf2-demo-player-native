#!/usr/bin/env python3
"""Apply one exact text substitution to a source file, keeping every other byte.

Why this exists rather than `sed -i`: the files a mutation harness edits do not
agree with each other about line endings. In this tree `native/src/main.cpp` and
`native/src/viewmodel.cpp` are CRLF and `native/src/native_renderer.cpp` is LF. A
needle pasted from one file into a patch for another silently fails to match, and
`sed -i` on Windows rewrites the whole file to LF as a side effect, which makes a
later "the file was restored" comparison a comparison with a different file.

    python patch-source.py <file> <<'PATCH'
    the exact text to find
    %%
    the exact text to put there
    PATCH

The document is read from stdin, the needle and the replacement are separated by
a line containing only `%%`, and both are matched against the file's own newline
convention, so a snippet can be pasted without knowing which one that file uses.

Exactly one occurrence is replaced. The tool refuses to succeed when the needle
is absent (exit 1, "pattern not found") or when the replacement would leave the
file unchanged (exit 1, "the replacement did not change the file"), so a caller
can treat exit 0 as "this defect is in the file now" rather than "something ran".

Everything is read and written as bytes: the files carry non-ASCII text, and a
decoding round-trip is a way to corrupt the very thing the comparison is about.
"""

import sys

# The line that separates the two halves of the document. A line, not a
# substring, because a C++ file can legitimately contain `%%` inside a format
# string and this tool must not split on that.
SEPARATOR = b"%%"


def fail(message):
    sys.stderr.write(message + "\n")
    sys.exit(1)


def split_document(document):
    lines = document.split(b"\n")
    for index, line in enumerate(lines):
        if line.rstrip(b"\r") == SEPARATOR:
            needle = b"\n".join(lines[:index])
            replacement = b"\n".join(lines[index + 1:])
            return needle, replacement
    fail("the patch document has no %% separator line")


def strip_one_newline(block):
    # The heredoc terminates with a newline that belongs to the shell, not to the
    # patch: `Box a;\n%%\n` names six characters, not seven.
    if block.endswith(b"\r\n"):
        return block[:-2]
    if block.endswith(b"\n"):
        return block[:-1]
    return block


def main():
    if len(sys.argv) != 2:
        fail("usage: patch-source.py <file>  (the patch document is read from stdin)")
    path = sys.argv[1]

    document = sys.stdin.buffer.read()
    # A document written by an editor on Windows arrives as CRLF; the comparison
    # below puts the file's own convention back, so normalize here and only here.
    document = document.replace(b"\r\n", b"\n")
    needle, replacement = split_document(document)
    needle = strip_one_newline(needle)
    replacement = strip_one_newline(replacement)
    if not needle:
        fail("the patch document has an empty needle")

    try:
        with open(path, "rb") as handle:
            text = handle.read()
    except OSError as error:
        fail("cannot read %s: %s" % (path, error))

    newline = b"\r\n" if b"\r\n" in text else b"\n"
    # Try the file's convention first, then the needle as written, so a patch that
    # is already correct still applies to a file whose endings were converted.
    candidates = [needle.replace(b"\n", newline), needle]
    for candidate in candidates:
        if candidate in text:
            break
    else:
        fail("pattern not found in %s" % path)

    occurrences = text.count(candidate)
    if occurrences != 1:
        # More than one site means the caller's needle does not identify the
        # defect. Refusing is the only safe answer: a harness that mutated the
        # wrong call site would report a catch it never measured.
        fail("the needle matches %d sites in %s, expected exactly 1" % (occurrences, path))

    patched = text.replace(candidate, replacement.replace(b"\n", newline), 1)
    if patched == text:
        fail("the replacement did not change the file")

    with open(path, "wb") as handle:
        handle.write(patched)


if __name__ == "__main__":
    main()
