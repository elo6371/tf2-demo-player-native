# Large demo scan crash evidence (2026-10-04)

The native player was started with an explicit TF2 resource root, the local
`73.dem`, `--audio-device 0`, `--start-paused`, and `--no-vsync`.

Observed:

```text
exit=-1073741571
```

`-1073741571` is Windows `0xC00000FD` (stack overflow). A small local demo and
the same executable without `--demo` stayed alive during the same startup
window. Temporarily skipping demo scanning made the large demo stay alive;
this isolated the fault to `scanKnownDemoMessages`, but did not identify the
specific message family. The temporary environment switches used for that
binary search were removed and are not product behavior.

The parser now has hard recursion limits for SendTable flattening and nested
SendProp values. The large-demo reproduction still requires a debugger stack
trace before claiming a complete fix.
