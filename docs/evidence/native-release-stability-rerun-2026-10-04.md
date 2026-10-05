# Native release stability rerun (2026-10-04)

This rerun did not change runtime code and did not emit audio.

The invalid WinMM device path exits before window/audio initialization:

```text
has_exited=True
exit_code=13
```

The cache path was checked afterward. The application created
`%LOCALAPPDATA%\TF2 Demo Player\cache`; it contained no leftover
`.write-test` or other temporary files:

```text
cache_exists=True
```

The previously verified clean install smoke remains passing with
`install_executable=1`, `tf_root_missing_prompt=1`, `warp_fallback=1`,
`feature_level=b100`, `install_layout=ok`, and exit code `0`.

The missing-TF-root and malformed-Demo GUI cases require an external desktop
harness to send `WM_CLOSE`/close the window and then inspect the natural exit
code. A synchronous shell invocation cannot distinguish a healthy message
loop from a hung window while it is still running. Long-duration packaged-app
startup/exit and real-device playback therefore remain external-machine
acceptance items.
