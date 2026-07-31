// =============================================================================
// tb_ram_basic.sv  -  Testbench dirigido para ram_axi4.sv
//
// Tests incluidos:
//   TEST 1 - Single write / single read
//   TEST 2 - Burst write 8 beats / burst read 8 beats
//   TEST 3 - Byte enables parciales (wstrb)
//   TEST 4 - bready demorado 5 ciclos (back-pressure canal B)
//   TEST 5 - Burst 32 beats
// =============================================================================
`timescale 1ns/1ps

module tb_ram_basic;

    // ---- Parametros ---------------------------------------------------------
    localparam int DATA_WIDTH = 32;
    localparam int ADDR_WIDTH = 26;
    localparam int ID_WIDTH   = 4;
    localparam int STRB_WIDTH = DATA_WIDTH / 8;
    localparam int CLK_PERIOD = 10;

    // ---- Clock y reset ------------------------------------------------------
    logic clk = 0;
    logic rst = 0;

    always #(CLK_PERIOD/2) clk = ~clk;

    // ---- Senales AXI4 -------------------------------------------------------
    logic [ID_WIDTH-1:0]     awid;
    logic [ADDR_WIDTH-1:0]   awaddr;
    logic [7:0]              awlen;
    logic [2:0]              awsize;
    logic [1:0]              awburst;
    logic                    awvalid;
    logic                    awready;

    logic [DATA_WIDTH-1:0]   wdata;
    logic [STRB_WIDTH-1:0]   wstrb;
    logic                    wlast;
    logic                    wvalid;
    logic                    wready;

    logic [ID_WIDTH-1:0]     bid;
    logic [1:0]              bresp;
    logic                    bvalid;
    logic                    bready;

    logic [ID_WIDTH-1:0]     arid;
    logic [ADDR_WIDTH-1:0]   araddr;
    logic [7:0]              arlen;
    logic [2:0]              arsize;
    logic [1:0]              arburst;
    logic                    arvalid;
    logic                    arready;

    logic [ID_WIDTH-1:0]     rid;
    logic [DATA_WIDTH-1:0]   rdata;
    logic [1:0]              rresp;
    logic                    rlast;
    logic                    rvalid;
    logic                    rready;

    // ---- DUT ----------------------------------------------------------------
    ram_axi4 #(
        .DATA_WIDTH(DATA_WIDTH),
        .ADDR_WIDTH(ADDR_WIDTH),
        .ID_WIDTH  (ID_WIDTH)
    ) dut (
        .clk     (clk),     .rst     (rst),
        .awid    (awid),    .awaddr  (awaddr),  .awlen   (awlen),
        .awsize  (awsize),  .awburst (awburst), .awvalid (awvalid), .awready(awready),
        .wdata   (wdata),   .wstrb   (wstrb),   .wlast   (wlast),
        .wvalid  (wvalid),  .wready  (wready),
        .bid     (bid),     .bresp   (bresp),   .bvalid  (bvalid),  .bready (bready),
        .arid    (arid),    .araddr  (araddr),  .arlen   (arlen),
        .arsize  (arsize),  .arburst (arburst), .arvalid (arvalid), .arready(arready),
        .rid     (rid),     .rdata   (rdata),   .rresp   (rresp),
        .rlast   (rlast),   .rvalid  (rvalid),  .rready  (rready)
    );

    // ---- Contadores ---------------------------------------------------------
    int tests_run    = 0;
    int tests_passed = 0;
    int tests_failed = 0;

    // =========================================================================
    // TASK: axi4_write
    //   AW -> W beats -> B response.
    // =========================================================================
    task automatic axi4_write(
        input logic [ID_WIDTH-1:0]   t_id,
        input logic [ADDR_WIDTH-1:0] t_addr,
        input logic [DATA_WIDTH-1:0] t_data [],
        input logic [STRB_WIDTH-1:0] t_strb [],
        input logic [1:0]            t_burst        = 2'b01,
        input int                    t_bready_delay = 0
    );
        automatic int beats = t_data.size();

        // -- Write Address ----------------------------------------------------
        @(posedge clk);
        awid    <= t_id;
        awaddr  <= t_addr;
        awlen   <= beats - 1;
        awsize  <= 3'h2;
        awburst <= t_burst;
        awvalid <= 1'b1;

        @(posedge clk);
        while (!awready) @(posedge clk);
        awvalid <= 1'b0;

        // -- Write Data (pipelined) -------------------------------------------
        // Pre-drive beat 0 ANTES del loop para que el RTL vea datos validos
        // en el mismo posedge donde wvalid=1 aparece por primera vez.
        // En cada handshake se actualiza wdata con el siguiente beat,
        // evitando escrituras con datos viejos.
        @(posedge clk);
        wdata  <= t_data[0];
        wstrb  <= t_strb[0];
        wlast  <= (beats == 1);
        wvalid <= 1'b1;

        for (int i = 0; i < beats; i++) begin
            // Esperar handshake del beat actual
            @(posedge clk);
            while (!wready) @(posedge clk);

            // En el mismo posedge del handshake, pre-drive el siguiente beat
            if (i < beats - 1) begin
                wdata <= t_data[i+1];
                wstrb <= t_strb[i+1];
                wlast <= (i+1 == beats-1);
                // wvalid se mantiene en 1
            end else begin
                wvalid <= 1'b0;
                wlast  <= 1'b0;
            end
        end

        // -- Write Response ---------------------------------------------------
        if (t_bready_delay > 0)
            repeat(t_bready_delay) @(posedge clk);

        bready <= 1'b1;
        @(posedge clk);
        while (!bvalid) @(posedge clk);
        bready <= 1'b0;

    endtask

    // =========================================================================
    // TASK: axi4_read
    //   AR -> R beats. Mismo patron de handshake que axi4_write.
    // =========================================================================
    task automatic axi4_read(
        input  logic [ID_WIDTH-1:0]   t_id,
        input  logic [ADDR_WIDTH-1:0] t_addr,
        input  int                    t_beats,
        output logic [DATA_WIDTH-1:0] rd_data [],
        input  logic [1:0]            t_burst = 2'b01
    );
        rd_data = new[t_beats];

        // -- Read Address -----------------------------------------------------
        @(posedge clk);
        arid    <= t_id;
        araddr  <= t_addr;
        arlen   <= t_beats - 1;
        arsize  <= 3'h2;
        arburst <= t_burst;
        arvalid <= 1'b1;

        @(posedge clk);
        while (!arready) @(posedge clk);
        arvalid <= 1'b0;

        // -- Read Data --------------------------------------------------------
        rready <= 1'b1;
        for (int i = 0; i < t_beats; i++) begin
            @(posedge clk);
            while (!rvalid) @(posedge clk);
            rd_data[i] = rdata;
        end
        rready <= 1'b0;

    endtask

    // ---- Macro CHECK --------------------------------------------------------
    `define CHECK(desc, cond) \
        tests_run++; \
        if (cond) begin \
            $display("  [PASS] %s", desc); \
            tests_passed++; \
        end else begin \
            $display("  [FAIL] %s", desc); \
            tests_failed++; \
        end

    // ---- Variables de test --------------------------------------------------
    logic [DATA_WIDTH-1:0] wr_data [];
    logic [STRB_WIDTH-1:0] wr_strb [];
    logic [DATA_WIDTH-1:0] rd_data [];

    // =========================================================================
    // SECUENCIA DE TESTS
    // =========================================================================
    initial begin
        awvalid = 0; awid = 0; awaddr = 0; awlen = 0; awsize = 0; awburst = 0;
        wvalid  = 0; wdata = 0; wstrb  = 0; wlast  = 0;
        bready  = 0;
        arvalid = 0; arid  = 0; araddr = 0; arlen  = 0; arsize = 0; arburst = 0;
        rready  = 0;

        rst = 0;
        repeat(5) @(posedge clk);
        rst = 1;
        repeat(2) @(posedge clk);

        $display("\n=== tb_ram_basic: inicio de tests ===\n");

        // ---------------------------------------------------------------------
        // TEST 1: Single write / single read
        // ---------------------------------------------------------------------
        $display("-- TEST 1: Single write / single read --");

        wr_data = new[1]; wr_strb = new[1];
        wr_data[0] = 32'hDEAD_BEEF;
        wr_strb[0] = 4'hF;
        axi4_write(4'h1, 26'h00_0100, wr_data, wr_strb);

        axi4_read(4'h1, 26'h00_0100, 1, rd_data);
        `CHECK("rd_data[0] == 0xDEAD_BEEF", rd_data[0] == 32'hDEAD_BEEF)

        // ---------------------------------------------------------------------
        // TEST 2: Burst write 8 beats / burst read 8 beats
        // ---------------------------------------------------------------------
        $display("-- TEST 2: Burst write/read 8 beats --");

        wr_data = new[8]; wr_strb = new[8];
        for (int i = 0; i < 8; i++) begin
            wr_data[i] = 32'hA000_0000 + i;
            wr_strb[i] = 4'hF;
        end
        axi4_write(4'h2, 26'h00_0200, wr_data, wr_strb);

        axi4_read(4'h2, 26'h00_0200, 8, rd_data);
        for (int i = 0; i < 8; i++) begin
            `CHECK($sformatf("burst rd_data[%0d] == 0xA000_%04h", i, i),
                   rd_data[i] == 32'hA000_0000 + i)
        end

        // ---------------------------------------------------------------------
        // TEST 3: Byte enables parciales
        //   Escribir 0xFFFF_FFFF, reescribir bytes [3:2] con strb=0xC
        //   Esperado: 0xCAFE_FFFF
        // ---------------------------------------------------------------------
        $display("-- TEST 3: Byte enables parciales --");

        wr_data = new[1]; wr_strb = new[1];
        wr_data[0] = 32'hFFFF_FFFF;
        wr_strb[0] = 4'hF;
        axi4_write(4'h3, 26'h00_0300, wr_data, wr_strb);

        wr_data[0] = 32'hCAFE_0000;
        wr_strb[0] = 4'hC;
        axi4_write(4'h3, 26'h00_0300, wr_data, wr_strb);

        axi4_read(4'h3, 26'h00_0300, 1, rd_data);
        `CHECK("strb parcial: resultado == 0xCAFE_FFFF",
               rd_data[0] == 32'hCAFE_FFFF)

        // ---------------------------------------------------------------------
        // TEST 4: bready demorado 5 ciclos (back-pressure canal B)
        //   Se usa t_bready_delay en el task para evitar race conditions.
        // ---------------------------------------------------------------------
        $display("-- TEST 4: bready demorado (5 ciclos) --");

        wr_data = new[1]; wr_strb = new[1];
        wr_data[0] = 32'hBEEF_1234;
        wr_strb[0] = 4'hF;
        axi4_write(.t_id(4'h4), .t_addr(26'h00_0400),
                   .t_data(wr_data), .t_strb(wr_strb),
                   .t_bready_delay(5));

        axi4_read(4'h4, 26'h00_0400, 1, rd_data);
        `CHECK("bready demorado: rd == 0xBEEF_1234",
               rd_data[0] == 32'hBEEF_1234)

        // ---------------------------------------------------------------------
        // TEST 5: Burst 32 beats
        // ---------------------------------------------------------------------
        $display("-- TEST 5: Burst 32 beats --");

        wr_data = new[32]; wr_strb = new[32];
        for (int i = 0; i < 32; i++) begin
            wr_data[i] = 32'hC0DE_0000 + i;
            wr_strb[i] = 4'hF;
        end
        axi4_write(4'h5, 26'h00_1000, wr_data, wr_strb);

        axi4_read(4'h5, 26'h00_1000, 32, rd_data);
        begin
            automatic int all_ok = 1;
            for (int i = 0; i < 32; i++) begin
                if (rd_data[i] !== 32'hC0DE_0000 + i) begin
                    $display("  [FAIL] beat %0d: exp=0x%08h got=0x%08h",
                             i, 32'hC0DE_0000 + i, rd_data[i]);
                    all_ok = 0;
                    tests_failed++;
                    tests_run++;
                end
            end
            if (all_ok) begin
                $display("  [PASS] burst32: todos los 32 beats correctos");
                tests_passed++;
                tests_run++;
            end
        end

        // -- Resumen ----------------------------------------------------------
        repeat(5) @(posedge clk);
        $display("\n=== RESUMEN ===");
        $display("  Tests corridos : %0d", tests_run);
        $display("  PASSED         : %0d", tests_passed);
        $display("  FAILED         : %0d", tests_failed);
        if (tests_failed == 0)
            $display("  [OK] Todos los tests pasaron\n");
        else
            $display("  [!!] %0d test(s) fallaron\n", tests_failed);

        $finish;
    end

    // ---- Timeout ------------------------------------------------------------
    initial begin
        #500_000;
        $display("[TIMEOUT] Simulacion excedio 500us");
        $finish;
    end

    // ---- Waveforms ----------------------------------------------------------
    initial begin
        $dumpfile("tb_ram_basic.vcd");
        $dumpvars(0, tb_ram_basic);
    end

endmodule