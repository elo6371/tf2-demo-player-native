# T54 -- why the WebView2 SDK is vendored, and what the licence requires

Answer to "why can't the 2.7 MB WebView2 SDK be downloaded at build time, or made
optional, and what does LICENSE/NOTICE coverage look like".

## What the 2.7 MB is

`native/third_party/webview2/` holds six tracked files:

| file | size |
|---|---|
| `include/WebView2.h` | 61072 lines, ~2.5 MB |
| `include/WebView2EnvironmentOptions.h` | small |
| `x64/WebView2Loader.dll` | 159320 B |
| `LICENSE.txt` | 27 lines, BSD-3-Clause (Microsoft) |
| `NOTICE.txt` | 85 lines, third-party notices from the NuGet package |
| (that is the whole vendored tree) | |

Only **one** of those files ships: the loader, 159 KB. The browser itself is the
Evergreen WebView2 runtime that Windows already carries -- the package does not
stage a fixed-version runtime, which is why the panel still installs and runs on a
host without it and reports `Unavailable` with a reason and a named fallback.
The 2.7 MB is a **build input**: the headers are what the C++ compiles against.

## Why not download it at build time

1. **The header is a compile-time input.** "Optional" would mean the product
   cannot be built without network access; "condititional" would mean two
   different products, one of which has no panel, and every gate assertion about
   the panel would be untestable in the other one.
2. **The build has to be the same in six months.** A download URL is not a
   version pin. A vendored header is: `git log -- native/third_party/webview2`
   answers "which SDK built this binary", and the answer cannot change under us.
3. **Locked builds are offline by design.** Every build in this repo runs inside
   `_shared/hostlock.sh`; a network fetch during a locked build turns a
   reproducible step into one that depends on a third party being up.
4. **No credentials on this host.** The SDK is public, but build-time fetch
   infrastructure (proxy, TLS trust store, retry policy) is a new failure mode
   that buys nothing: the files are 2.7 MB, once.

## What the licence requires, and where it is

`LICENSE.txt` is BSD-3-Clause. Clause 2 is the one that matters here: a
**binary** redistribution must reproduce the copyright notice, the conditions and
the disclaimer in the documentation and/or other materials accompanying the
distribution. We redistribute `WebView2Loader.dll`, so the notice has to travel
with the package.

- Vendored beside the headers it licenses: `native/third_party/webview2/LICENSE.txt`,
  `NOTICE.txt` (the NuGet package's third-party notices travel with the SDK).
- Installed into the package as documentation:
  `install(FILES .../LICENSE.txt .../NOTICE.txt DESTINATION third_party/webview2)`
  (the actual landed path is read out of a staged install, not out of the rule --
  see the run's `install-preview-files.txt`).
- **Not** in `.uninstall/payload`: the payload is the set of files the installer
  lays down and later owns, and its count is a constant another gate pins
  (`install-lifecycle-check.sh:204`). A licence is read, not installed; putting it
  in the payload would make the installer treat it as runtime state it must back
  up, restore and uninstall, which it is not.

The Evergreen runtime itself is Microsoft's and is not redistributed by this
package, so no runtime licence file is shipped -- the loader's licence is.

## Status

- The vendored tree, the loader-only install rules and the licence install rule
  are in `native/CMakeLists.txt`.
- **NOT VERIFIED**: that the two licence files land in the staged package. The
  locked run executes `cmake --install --prefix .scratch/t54-install-preview` and
  lists the tree; the report cites that listing, or -- if a target this run did not
  build blocks the install -- the generated `cmake_install.cmake` rule, labelled
  as a rule and not as a landed file.
