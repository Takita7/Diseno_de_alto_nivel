// =============================================================================
// tb_top.sv  -  Top-level UVM testbench para ram_axi4
//
// Tests disponibles via +UVM_TESTNAME:
//   smoke_test   (default)  - write-read + byte enables
//   burst_test              - 32 beats
//   stress_test             - 20 transacciones aleatorias
//
// EDA Playground (VCS):
//   Design:    ram_axi4.sv
//   Testbench: axi4_if.sv, axi4_seq_item.sv, axi4_driver.sv,
//              axi4_monitor.sv, axi4_agent.sv, axi4_scoreboard.sv,
//              axi4_coverage.sv, axi4_sequences.sv, axi4_env.sv,
//              axi4_test.sv, axi4_pkg.sv, tb_top.sv
//   Top: tb_top
//   Comp Flags: -timescale=1ns/1ns +vcs+flush+all +warn=all -sverilog -ntb_opts uvm-1.2 +define+UVM_NO_DPI
//   Run  Flags: +UVM_TESTNAME=<test>
// =============================================================================
`timescale 1ns/1ps
`include "uvm_macros.svh"
import uvm_pkg::*;
`include "axi4_if.sv"   
`include "axi4_pkg.sv" 

module tb_top;

    localparam int CLK_PERIOD = 10;

    logic clk    = 0;
    logic rst = 0;

    always #(CLK_PERIOD/2) clk = ~clk;

    initial begin
        rst = 0;
        repeat(5) @(posedge clk);
        rst = 1;
    end

    // ---- Interface ----------------------------------------------------------
    axi4_if #(.DATA_WIDTH(32), .ADDR_WIDTH(26), .ID_WIDTH(4))
        axi4_bus (.clk(clk), .rst(rst));

    // ---- DUT ----------------------------------------------------------------
    ram_axi4 #(.DATA_WIDTH(32), .ADDR_WIDTH(26), .ID_WIDTH(4)) dut (
        .clk     (clk),               .rst      (rst),
        .awid    (axi4_bus.awid),     .awaddr   (axi4_bus.awaddr),
        .awlen   (axi4_bus.awlen),    .awsize   (axi4_bus.awsize),
        .awburst (axi4_bus.awburst),  .awvalid  (axi4_bus.awvalid),
        .awready (axi4_bus.awready),
        .wdata   (axi4_bus.wdata),    .wstrb    (axi4_bus.wstrb),
        .wlast   (axi4_bus.wlast),    .wvalid   (axi4_bus.wvalid),
        .wready  (axi4_bus.wready),
        .bid     (axi4_bus.bid),      .bresp    (axi4_bus.bresp),
        .bvalid  (axi4_bus.bvalid),   .bready   (axi4_bus.bready),
        .arid    (axi4_bus.arid),     .araddr   (axi4_bus.araddr),
        .arlen   (axi4_bus.arlen),    .arsize   (axi4_bus.arsize),
        .arburst (axi4_bus.arburst),  .arvalid  (axi4_bus.arvalid),
        .arready (axi4_bus.arready),
        .rid     (axi4_bus.rid),      .rdata    (axi4_bus.rdata),
        .rresp   (axi4_bus.rresp),    .rlast    (axi4_bus.rlast),
        .rvalid  (axi4_bus.rvalid),   .rready   (axi4_bus.rready)
    );

    // ---- Config DB ----------------------------------------------------------
    initial begin
        uvm_config_db #(virtual axi4_if.DRIVER)::set(
            null, "uvm_test_top.*", "vif", axi4_bus);
        uvm_config_db #(virtual axi4_if.MONITOR)::set(
            null, "uvm_test_top.*", "vif", axi4_bus);
        run_test();
    end

    // ---- Timeout ------------------------------------------------------------
    initial begin
        #2_000_000;
        `uvm_fatal("TIMEOUT", "Simulacion excedio 2ms")
    end

    // ---- Waveforms ----------------------------------------------------------
    initial begin
        $dumpfile("tb_top.vcd");
        $dumpvars(0, tb_top);
    end

endmodule : tb_top