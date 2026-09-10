#pragma once

// GIT_VERSION is injected as a quoted string literal by the top-level CMakeLists
// (git describe --tags --always --dirty), replacing the old genVersion.py flow.
#ifndef GIT_VERSION
#define GIT_VERSION "undefined version"
#endif
