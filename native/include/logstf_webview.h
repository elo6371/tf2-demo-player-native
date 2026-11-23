#pragma once

// T54 -- the WebView2 backend's factory.
//
// Declared apart from logstf_viewer.h because only the product links this: the
// probe includes logstf_viewer.h alone, so a target without the WebView2 SDK
// compiles and runs, and the panel's rules stay testable on a machine that has
// no runtime at all.

#include "logstf_viewer.h"

#include <string>

namespace tf2::native {

// The one factory the product uses. Returns a host bound to WebView2; the
// caller owns it. Never returns nullptr (the object reports its own failures
// through HostStatus), because "no host object" and "a host that could not
// start" produce different messages and the panel shows the second one.
ILogsViewHost* createWebView2LogsHost();

// "loader=present browser=155.0.4283.62" or "loader=missing (path)". The loader
// DLL is loaded by name at run time (the runtime itself is Windows'), so this
// answers whether the one file the package ships next to the executable is
// there and exports what the viewer calls. Empty when the backend is not
// compiled into the target.
std::string webView2LoaderStatus();

} // namespace tf2::native
