// =============================================================================
// ram_axi4.sv  –  Memoria RAM 64 MB con interfaz AXI4 Full
//
// Parámetros:
//   DATA_WIDTH  –  ancho del bus de datos en bits  (default: 32)
//   ADDR_WIDTH  –  bits de dirección byte-addressable (default: 26 → 64 MB)
//   ID_WIDTH    –  ancho del campo ID  (default: 4)
//
// Protocolo:
//   - Burst type INCR (2'b01) soportado completamente
//   - Burst type FIXED (2'b00) soportado (dirección no avanza)
//   - WRAP no soportado en esta versión (se trata como INCR)
//   - awsize/arsize ignorados; se asume un beat = DATA_WIDTH/8 bytes
//   - Byte enables (wstrb) aplicados correctamente
//
// Paths independientes:
//   Write: AW → W beats → B response
//   Read:  AR → R beats
//   Ambos paths corren en paralelo sin interferencia.
// =============================================================================
`timescale 1ns/1ps

module ram_axi4 #(
    parameter int DATA_WIDTH = 32,
    parameter int ADDR_WIDTH = 26,
    parameter int ID_WIDTH   = 4
) (
    input  logic clk,
    input  logic rst,   // reset activo en bajo

    // ── Write Address Channel (AW) ────────────────────────────────────────────
    input  logic [ID_WIDTH-1:0]     awid,
    input  logic [ADDR_WIDTH-1:0]   awaddr,
    input  logic [7:0]              awlen,    // beats = awlen + 1
    input  logic [2:0]              awsize,
    input  logic [1:0]              awburst,
    input  logic                    awvalid,
    output logic                    awready,

    // ── Write Data Channel (W) ────────────────────────────────────────────────
    input  logic [DATA_WIDTH-1:0]   wdata,
    input  logic [DATA_WIDTH/8-1:0] wstrb,
    input  logic                    wlast,
    input  logic                    wvalid,
    output logic                    wready,

    // ── Write Response Channel (B) ────────────────────────────────────────────
    output logic [ID_WIDTH-1:0]     bid,
    output logic [1:0]              bresp,
    output logic                    bvalid,
    input  logic                    bready,

    // ── Read Address Channel (AR) ─────────────────────────────────────────────
    input  logic [ID_WIDTH-1:0]     arid,
    input  logic [ADDR_WIDTH-1:0]   araddr,
    input  logic [7:0]              arlen,
    input  logic [2:0]              arsize,
    input  logic [1:0]              arburst,
    input  logic                    arvalid,
    output logic                    arready,

    // ── Read Data Channel (R) ─────────────────────────────────────────────────
    output logic [ID_WIDTH-1:0]     rid,
    output logic [DATA_WIDTH-1:0]   rdata,
    output logic [1:0]              rresp,
    output logic                    rlast,
    output logic                    rvalid,
    input  logic                    rready
);

    // ── Constantes locales ───────────────────────────────────────────────────
    localparam int STRB_WIDTH = DATA_WIDTH / 8;             // bytes por beat
    localparam int WORD_BITS  = $clog2(STRB_WIDTH);         // bits de offset intrapalabra
    localparam int MEM_DEPTH  = (1 << ADDR_WIDTH) / STRB_WIDTH; // palabras totales

    // ── Memoria ──────────────────────────────────────────────────────────────
    logic [DATA_WIDTH-1:0] mem [0:MEM_DEPTH-1];

    // =========================================================================
    // WRITE PATH
    // FSM: WS_IDLE → WS_DATA → WS_RESP → WS_IDLE
    // =========================================================================
    typedef enum logic [1:0] {
        WS_IDLE = 2'b00,
        WS_DATA = 2'b01,
        WS_RESP = 2'b10
    } w_state_e;

    w_state_e w_state;

    logic [ID_WIDTH-1:0]   wid_r;
    logic [ADDR_WIDTH-1:0] waddr_r;
    logic [1:0]            wburst_r;

    // Próxima dirección de escritura (combinacional)
    logic [ADDR_WIDTH-1:0] w_next_addr;
    always_comb begin
        w_next_addr = (wburst_r == 2'b01) ? waddr_r + ADDR_WIDTH'(STRB_WIDTH)
                                           : waddr_r;
    end

    always_ff @(posedge clk or negedge rst) begin
        if (!rst) begin
            w_state <= WS_IDLE;
            awready <= 1'b1;
            wready  <= 1'b0;
            bvalid  <= 1'b0;
            bid     <= '0;
            bresp   <= 2'b00;
            wid_r   <= '0;
            waddr_r <= '0;
            wburst_r<= 2'b01;
        end else begin
            case (w_state)

                // ── Esperar dirección de escritura ───────────────────────────
                WS_IDLE: begin
                    bvalid <= 1'b0;
                    if (awvalid && awready) begin
                        wid_r    <= awid;
                        waddr_r  <= awaddr;
                        wburst_r <= awburst;
                        awready  <= 1'b0;
                        wready   <= 1'b1;
                        w_state  <= WS_DATA;
                    end
                end

                // ── Consumir beats de escritura ──────────────────────────────
                WS_DATA: begin
                    if (wvalid && wready) begin
                        // Aplicar byte enables al write
                        for (int i = 0; i < STRB_WIDTH; i++) begin
                            if (wstrb[i])
                                mem[waddr_r[ADDR_WIDTH-1:WORD_BITS]][i*8 +: 8]
                                    <= wdata[i*8 +: 8];
                        end
                        waddr_r <= w_next_addr;

                        if (wlast) begin
                            wready  <= 1'b0;
                            bvalid  <= 1'b1;
                            bid     <= wid_r;
                            bresp   <= 2'b00;  // OKAY
                            w_state <= WS_RESP;
                        end
                    end
                end

                // ── Enviar respuesta y volver a IDLE ─────────────────────────
                WS_RESP: begin
                    if (bvalid && bready) begin
                        bvalid  <= 1'b0;
                        awready <= 1'b1;
                        w_state <= WS_IDLE;
                    end
                end

                default: w_state <= WS_IDLE;
            endcase
        end
    end

    // =========================================================================
    // READ PATH
    // FSM: RS_IDLE → RS_DATA → RS_IDLE
    // El primer beat se presenta en el mismo ciclo en que llega arvalid.
    // =========================================================================
    typedef enum logic [1:0] {
        RS_IDLE = 2'b00,
        RS_DATA = 2'b01
    } r_state_e;

    r_state_e r_state;

    logic [ID_WIDTH-1:0]   rid_r;
    logic [ADDR_WIDTH-1:0] raddr_r;
    logic [7:0]            rlen_r;
    logic [7:0]            rbeat_r;
    logic [1:0]            rburst_r;

    // Próxima dirección de lectura (combinacional)
    logic [ADDR_WIDTH-1:0] r_next_addr;
    always_comb begin
        r_next_addr = (rburst_r == 2'b01) ? raddr_r + ADDR_WIDTH'(STRB_WIDTH)
                                           : raddr_r;
    end

    always_ff @(posedge clk or negedge rst) begin
        if (!rst) begin
            r_state  <= RS_IDLE;
            arready  <= 1'b1;
            rvalid   <= 1'b0;
            rlast    <= 1'b0;
            rid      <= '0;
            rdata    <= '0;
            rresp    <= 2'b00;
            rbeat_r  <= '0;
            rid_r    <= '0;
            raddr_r  <= '0;
            rlen_r   <= '0;
            rburst_r <= 2'b01;
        end else begin
            case (r_state)

                // ── Esperar dirección de lectura y presentar primer beat ─────
                RS_IDLE: begin
                    if (arvalid && arready) begin
                        rid_r    <= arid;
                        raddr_r  <= araddr;
                        rlen_r   <= arlen;
                        rburst_r <= arburst;
                        rbeat_r  <= '0;
                        arready  <= 1'b0;
                        // Presentar primer beat
                        rid      <= arid;
                        rdata    <= mem[araddr[ADDR_WIDTH-1:WORD_BITS]];
                        rresp    <= 2'b00;
                        rlast    <= (arlen == 8'h00);
                        rvalid   <= 1'b1;
                        r_state  <= RS_DATA;
                    end
                end

                // ── Entregar beats al master ──────────────────────────────────
                RS_DATA: begin
                    if (rvalid && rready) begin
                        if (rlast) begin
                            // Último beat entregado
                            rvalid  <= 1'b0;
                            arready <= 1'b1;
                            r_state <= RS_IDLE;
                        end else begin
                            // Preparar siguiente beat
                            rbeat_r <= rbeat_r + 8'h01;
                            raddr_r <= r_next_addr;
                            rid     <= rid_r;
                            rdata   <= mem[r_next_addr[ADDR_WIDTH-1:WORD_BITS]];
                            rresp   <= 2'b00;
                            rlast   <= (rbeat_r + 8'h01 == rlen_r);
                        end
                    end
                end

                default: r_state <= RS_IDLE;
            endcase
        end
    end

    // =========================================================================
    // Senales AXI4 intencionalmente no usadas por este slave:
    //   awlen  : slave usa wlast para detectar ultimo beat (correcto per spec)
    //   awsize : ancho de bus fijo = DATA_WIDTH bits, no se necesita decodificar
    //   arsize : idem awsize para reads
    // El assign dummy evita warnings de Verilator sin afectar la simulacion.
    // =========================================================================
    logic _unused;
    assign _unused = &{awlen, awsize, arsize};

endmodule 
