// doctest_main.cpp — entry point for the streaming-parser test binary.
//
// dc_tests keeps DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN in dc_tests.cpp alongside
// the M0 trace goldens. dc_tests_streaming re-compiles a *subset* of that suite
// — the two files that exercise the parse seam — against the other
// implementation of parse_anvil_frame, so it needs a main of its own rather than
// dragging in trace-structure tests that have nothing to do with the swap.
//
// Deliberately nothing else lives here: every assertion in the streaming binary
// comes from a file dc_tests compiles too, which is what makes "same source, two
// link configs" a checkable claim rather than a description.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
