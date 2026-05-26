# Intentionally minimal.
#
# The Windows build flow patches generated archive rules after configure via
# tools/patch-ar-rules.ps1. This file only needs to exist early so CMake can
# honor CMAKE_USER_MAKE_RULES_OVERRIDE_C without failing during reconfigure.
