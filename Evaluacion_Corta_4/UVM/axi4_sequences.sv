// =============================================================================
// axi4_sequences.sv  -  Secuencias AXI4
//
// axi4_base_seq  - helpers do_write / do_read
// wr_rd_seq      - write + read de 1 beat
// burst_seq      - burst configurable (default 32 beats)
// strb_seq       - verifica byte enables parciales
// stress_seq     - N transacciones aleatorias write + read
// =============================================================================

// ---- Base sequence con helpers ----------------------------------------------
class axi4_base_seq extends uvm_sequence #(axi4_seq_item);
    `uvm_object_utils(axi4_base_seq)

    function new(string name = "axi4_base_seq");
        super.new(name);
    endfunction

    task do_write(bit [25:0] addr, bit [31:0] data [], bit [3:0] strb []);
        axi4_seq_item item = axi4_seq_item::type_id::create("wr_item");
        start_item(item);
        item.is_write   = 1;
        item.addr       = addr;
        item.burst_len  = data.size() - 1;
        item.burst_type = 2'b01;
        item.id         = $urandom & 4'hF;
        item.data       = data;
        item.strb       = strb;
        finish_item(item);
    endtask

    task do_read(bit [25:0] addr, int beats, output bit [31:0] data []);
        axi4_seq_item item = axi4_seq_item::type_id::create("rd_item");
        start_item(item);
        item.is_write   = 0;
        item.addr       = addr;
        item.burst_len  = beats - 1;
        item.burst_type = 2'b01;
        item.id         = $urandom & 4'hF;
        item.data       = new[beats];
        item.strb       = new[beats];
        foreach (item.strb[i]) item.strb[i] = 4'hF;
        finish_item(item);
        data = item.data;
    endtask
endclass : axi4_base_seq

// ---- Write-Read simple (1 beat) --------------------------------------------
class wr_rd_seq extends axi4_base_seq;
    `uvm_object_utils(wr_rd_seq)
    function new(string name = "wr_rd_seq"); super.new(name); endfunction

    task body();
        bit [31:0] wr_data [] = new[1];
        bit [3:0]  wr_strb [] = new[1];
        bit [31:0] rd_data [];
        wr_data[0] = 32'hDEAD_BEEF;
        wr_strb[0] = 4'hF;
        do_write(26'h00_1000, wr_data, wr_strb);
        do_read (26'h00_1000, 1, rd_data);
        `uvm_info("SEQ", $sformatf("wr_rd_seq: rd=0x%08h", rd_data[0]), UVM_MEDIUM)
    endtask
endclass : wr_rd_seq

// ---- Burst write-read (configurable, default 32 beats) ---------------------
class burst_seq extends axi4_base_seq;
    `uvm_object_utils(burst_seq)
    int unsigned num_beats = 32;
    function new(string name = "burst_seq"); super.new(name); endfunction

    task body();
        bit [31:0] wr_data [] = new[num_beats];
        bit [3:0]  wr_strb [] = new[num_beats];
        bit [31:0] rd_data [];
        for (int i = 0; i < num_beats; i++) begin
            wr_data[i] = 32'hC0DE_0000 + i;
            wr_strb[i] = 4'hF;
        end
        do_write(26'h00_2000, wr_data, wr_strb);
        do_read (26'h00_2000, num_beats, rd_data);
        `uvm_info("SEQ", $sformatf("burst_seq: %0d beats", num_beats), UVM_MEDIUM)
    endtask
endclass : burst_seq

// ---- Byte enables parciales ------------------------------------------------
class strb_seq extends axi4_base_seq;
    `uvm_object_utils(strb_seq)
    function new(string name = "strb_seq"); super.new(name); endfunction

    task body();
        bit [31:0] wr_data [] = new[1];
        bit [3:0]  wr_strb [] = new[1];
        bit [31:0] rd_data [];

        // Escribir 0xFFFF_FFFF completo
        wr_data[0] = 32'hFFFF_FFFF; wr_strb[0] = 4'hF;
        do_write(26'h00_3000, wr_data, wr_strb);

        // Reescribir solo bytes altos (strb=0xC -> bytes 3 y 2)
        wr_data[0] = 32'hCAFE_0000; wr_strb[0] = 4'hC;
        do_write(26'h00_3000, wr_data, wr_strb);

        do_read(26'h00_3000, 1, rd_data);
        `uvm_info("SEQ", $sformatf("strb_seq: rd=0x%08h (exp=0xCAFE_FFFF)",
            rd_data[0]), UVM_MEDIUM)
    endtask
endclass : strb_seq

// ---- Stress: N transacciones aleatorias write + read -----------------------
class stress_seq extends axi4_base_seq;
    `uvm_object_utils(stress_seq)
    int unsigned num_txns = 20;
    function new(string name = "stress_seq"); super.new(name); endfunction

    task body();
        axi4_seq_item item;
        bit [25:0] wr_addrs [] = new[num_txns];
        int        wr_beats [] = new[num_txns];

        // Writes aleatorios en regiones distintas
        for (int i = 0; i < num_txns; i++) begin
            item = axi4_seq_item::type_id::create("stress_wr");
            start_item(item);
            if (!item.randomize() with {
                is_write        == 1;
                burst_len       inside {[0:15]};
                addr[25:10]     == i[15:0];
                addr[1:0]       == 2'b00;
            })
                `uvm_fatal("SEQ", "Randomize failed en stress_seq")
            foreach (item.strb[j]) item.strb[j] = 4'hF;
            wr_addrs[i] = item.addr;
            wr_beats[i] = item.burst_len + 1;
            finish_item(item);
        end

        // Reads de verificacion
        for (int i = 0; i < num_txns; i++) begin
            bit [31:0] rd_data [];
            do_read(wr_addrs[i], wr_beats[i], rd_data);
        end
        `uvm_info("SEQ", $sformatf("stress_seq: %0d WR + %0d RD completados",
            num_txns, num_txns), UVM_MEDIUM)
    endtask
endclass : stress_seq