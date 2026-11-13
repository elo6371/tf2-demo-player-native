# Sourced by other scripts: puts cl.exe, nmake and the Windows SDK tools (rc.exe,
# mt.exe) on PATH and sets INCLUDE/LIB. vcvars64.bat cannot be used here because
# it calls reg.exe, which this sandbox blocks.
MSVC_VER="14.44.35207"
SDK_VER="10.0.26100.0"
MSVC_W="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/$MSVC_VER"
SDK_W="C:/Program Files (x86)/Windows Kits/10"
export PATH="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/$MSVC_VER/bin/Hostx64/x64:/c/Program Files (x86)/Windows Kits/10/bin/$SDK_VER/x64:$PATH"
export INCLUDE="$MSVC_W/include;$SDK_W/Include/$SDK_VER/ucrt;$SDK_W/Include/$SDK_VER/shared;$SDK_W/Include/$SDK_VER/um;$SDK_W/Include/$SDK_VER/winrt"
export LIB="$MSVC_W/lib/x64;$SDK_W/Lib/$SDK_VER/ucrt/x64;$SDK_W/Lib/$SDK_VER/um/x64"
