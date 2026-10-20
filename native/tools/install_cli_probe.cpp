// install_cli_probe -- drive the product's installer lifecycle from a script.
//
// WHY THIS IS A SEPARATE EXECUTABLE
//   The lifecycle itself (native/src/install_lifecycle.cpp) is product code: it
//   is linked into tf2_demo_native and is what a shipped package runs. This
//   probe is only the command line around it. A package is "these files, staged
//   next to the installer", so a gate stages them under
//   <install>/.uninstall/payload/ and this probe performs the install. That keeps
//   the payload an explicit, hashable input instead of a glob over whatever a
//   build directory happens to contain.
//
// USAGE
//   install_cli_probe --version
//   install_cli_probe --install   [--install-dir DIR] [--dry-run] [--verbose]
//   install_cli_probe --status    [--install-dir DIR]
//   install_cli_probe --uninstall [--install-dir DIR] [--dry-run]
//   --install-dir defaults to the current working directory.
// Exit 0 when the operation reported PASS/VERIFIED, 2 when it reported a named
// failure.
#include "install_lifecycle.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
  enum class Mode { None, Version, Install, Uninstall, Status };
  Mode mode = Mode::None;
  tf2::native::installer::Options options;
  bool haveDirectory = false;

  for (int i = 1; i < argc; ++i) {
    const char* argument = argv[i];
    if (std::strcmp(argument, "--version") == 0) { mode = Mode::Version; }
    else if (std::strcmp(argument, "--install") == 0) { mode = Mode::Install; }
    else if (std::strcmp(argument, "--uninstall") == 0) { mode = Mode::Uninstall; }
    else if (std::strcmp(argument, "--status") == 0) { mode = Mode::Status; }
    else if (std::strcmp(argument, "--dry-run") == 0) { options.dryRun = true; }
    else if (std::strcmp(argument, "--verbose") == 0) { options.verbose = true; }
    else if (std::strcmp(argument, "--install-dir") == 0 && i + 1 < argc) {
      options.installDir = std::filesystem::absolute(std::filesystem::u8path(argv[++i]));
      haveDirectory = true;
    } else {
      std::fprintf(stderr, "usage: %s --version | --install | --uninstall | --status"
        " [--install-dir DIR] [--dry-run] [--verbose]\n", argv[0]);
      return 2;
    }
  }
  if (!haveDirectory) {
    // The working directory: "install here" is the portable-package model this
    // product already has, not a Program Files location this round cannot test.
    std::error_code error;
    options.installDir = std::filesystem::current_path(error);
    if (error) {
      std::fprintf(stderr, "cannot resolve the working directory\n");
      return 2;
    }
  }

  tf2::native::installer::Result result;
  switch (mode) {
    case Mode::Version: result = tf2::native::installer::version(options); break;
    case Mode::Install: result = tf2::native::installer::install(options); break;
    case Mode::Uninstall: result = tf2::native::installer::uninstall(options); break;
    case Mode::Status: result = tf2::native::installer::status(options); break;
    case Mode::None:
    default:
      std::fprintf(stderr, "one of --version, --install, --uninstall or --status is required\n");
      return 2;
  }
  for (const auto& line : result.lines) std::printf("%s\n", line.c_str());
  std::printf("exit=%d\n", result.exitCode);
  return result.exitCode;
}
