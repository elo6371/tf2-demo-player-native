# Large demo stack overflow fix (2026-10-04)

The crash was reproduced under CDB as Windows `0xC00000FD`. The symbolized
stack pointed to `wWinMain -> sha256File -> __chkstk`; `sha256File` had a
1 MiB local `std::array`, which exceeded the default Windows thread stack
while hashing a large demo.

The streaming hash buffer now uses heap storage (`std::vector`) and keeps the
same chunked BCrypt SHA-256 behavior.

Verification with the local `73.dem`, explicit TF2 root, `--audio-device 0`,
`--start-paused`, and `--no-vsync`:

```text
before: exit=-1073741571 (0xC00000FD)
after:  release_alive=true after 4 seconds
```

No audio playback was requested during this check.
