// =============================================================================
// axi4_if.sv  -  Interface AXI4 Full
//
// Clocking blocks:
//   driver_cb  - master maneja (salidas al DUT + entradas del DUT)
//   monitor_cb - muestrea todas las senales
//
// =============================================================================
`timescale 1ns/1ps

interface axi4_if #(
    parameter int DATA_WIDTH = 32,
    parameter int ADDR_WIDTH = 26,
    parameter int ID_WIDTH   = 4
) (
    input logic clk,
    input logic rst
);
    localparam int STRB_WIDTH = DATA_WIDTH / 8;

    // ---- Write Address Channel ----------------------------------------------
    logic [ID_WIDTH-1:0]   awid;
    logic [ADDR_WIDTH-1:0] awaddr;
    logic [7:0]            awlen;
    logic [2:0]            awsize;
    logic [1:0]            awburst;
    logic                  awvalid;
    logic                  awready;

    // ---- Write Data Channel -------------------------------------------------
    logic [DATA_WIDTH-1:0] wdata;
    logic [STRB_WIDTH-1:0] wstrb;
    logic                  wlast;
    logic                  wvalid;
    logic                  wready;

    // ---- Write Response Channel ---------------------------------------------
    logic [ID_WIDTH-1:0]   bid;
    logic [1:0]            bresp;
    logic                  bvalid;
    logic                  bready;

    // ---- Read Address Channel -----------------------------------------------
    logic [ID_WIDTH-1:0]   arid;
    logic [ADDR_WIDTH-1:0] araddr;
    logic [7:0]            arlen;
    logic [2:0]            arsize;
    logic [1:0]            arburst;
    logic                  arvalid;
    logic                  arready;

    // ---- Read Data Channel --------------------------------------------------
    logic [ID_WIDTH-1:0]   rid;
    logic [DATA_WIDTH-1:0] rdata;
    logic [1:0]            rresp;
    logic                  rlast;
    logic                  rvalid;
    logic                  rready;

    // =========================================================================
    // Driver clocking block (master drives DUT inputs, samples DUT outputs)
    // =========================================================================
    clocking driver_cb @(posedge clk);
        default input #1 output #1;
        // Master drives
        output awid, awaddr, awlen, awsize, awburst, awvalid;
        output wdata, wstrb, wlast, wvalid;
        output bready;
        output arid, araddr, arlen, arsize, arburst, arvalid;
        output rready;
        // Master samples
        input  awready;
        input  wready;
        input  bid, bresp, bvalid;
        input  arready;
        input  rid, rdata, rresp, rlast, rvalid;
    endclocking

    // =========================================================================
    // Monitor clocking block 
    // =========================================================================
    clocking monitor_cb @(posedge clk);
        default input #1;
        input awid, awaddr, awlen, awsize, awburst, awvalid, awready;
        input wdata, wstrb, wlast, wvalid, wready;
        input bid, bresp, bvalid, bready;
        input arid, araddr, arlen, arsize, arburst, arvalid, arready;
        input rid, rdata, rresp, rlast, rvalid, rready;
    endclocking

    // ---- Modports -----------------------------------------------------------
    modport DRIVER  (clocking driver_cb,  input clk, rst);
    modport MONITOR (clocking monitor_cb, input clk, rst);

endinterface : axi4_if