// sc_gtest_main.cpp - sc_main entry point for SystemC+GTest executables
//
// SystemC replaces main() with its own entry point that calls sc_main().
// This file provides sc_main() which initialises and runs all GTest tests.

#include <gtest/gtest.h>
#include <systemc>
#include <iostream>

int sc_main(int argc, char* argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    sc_core::sc_stop();
    return result;
}
