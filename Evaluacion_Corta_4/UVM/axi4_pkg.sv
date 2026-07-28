// =============================================================================
// axi4_pkg.sv  -  Package UVM para RAM AXI4 Full
// =============================================================================
`timescale 1ns/1ps
`include "uvm_macros.svh"

package axi4_pkg;
    import uvm_pkg::*;

    `include "axi4_seq_item.sv"
    `include "axi4_driver.sv"
    `include "axi4_monitor.sv"
    `include "axi4_agent.sv"
    `include "axi4_scoreboard.sv"
    `include "axi4_coverage.sv"
    `include "axi4_sequences.sv"
    `include "axi4_env.sv"
    `include "axi4_test.sv"

endpackage : axi4_pkg