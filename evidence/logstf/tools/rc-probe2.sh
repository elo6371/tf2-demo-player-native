#!/usr/bin/env bash
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
# A native Windows SEH access violation (0xC0000005), raised deliberately: this is
# the shape a WebView2 callback fault would take, and the question run 2 asks is
# what MSYS reports for it.
"$PY" -c "import ctypes; ctypes.windll.kernel32.RaiseException(0xC0000005, 0, 0, None)" >/dev/null 2>&1
echo "native_av_rc=$?"
"$PY" -c "import sys; sys.exit(127)"
echo "exit127_rc=$?"
