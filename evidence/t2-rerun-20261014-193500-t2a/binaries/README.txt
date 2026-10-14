# The binaries behind these two runs

The build on this machine is **not byte-reproducible**. Three binaries were
produced from byte-identical source (`git diff -- native/` empty throughout,
and the gate's own `cmp -s` check confirmed each mutation's source came back
byte-identical), and all three differ:

| sha256 (prefix) | size | how it was produced | role |
|---|---|---|---|
| `cfaffb16` | 1,561,088 | already on disk at 19:24, before the rerun task started; step 2's `build-target.sh` compiled and linked **nothing** | **the binary the t2a forward pass and all four mutations actually measured** |
| `c0c7bb08` | 1,561,088 | `build-target.sh tf2_demo_native` after the mutation cycle's byte-restoration | the recovery rebuild; never measured by a gate run |
| `2729dc80` | 1,561,088 | `build-cmake.sh` (clean, 7 min 9 s, rc=0, 31 exes) | **the binary the t2b forward pass measured**; kept here |

What is kept, and what is not:

* `tf2_demo_native.c0c7bb08-post-restore-rebuild.exe` is kept, because it is the
  observation that makes the finding concrete: the same source bytes that
  produced `cfaffb16` produced this instead.
* **`cfaffb16` is not kept.** The file was overwritten by the mutation cycle's
  rebuild before anyone recorded that a binary's identity mattered. That is a
  defect in this run's evidence handling, not in the product, and it is the
  reason `exe-identity.txt` above says `preserved=no`.
* `tf2_demo_native.2729dc80.exe` is kept in the t2b run directory, next to the
  forward pass that measured it.

Why the verdict does not depend on the missing file: the t2b forward pass ran
against `2729dc80`, a different binary, and reproduced **all 101 assertions and
every measured byte count with no difference** (`diff` of the two forward logs
is empty), including all ten frames byte-for-byte. Two different binaries of the
same source produce identical readings and identical frames, so the differing
bytes are not in any path these readings observe.
