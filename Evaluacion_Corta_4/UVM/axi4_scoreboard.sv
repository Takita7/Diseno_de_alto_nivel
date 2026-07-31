// =============================================================================
// axi4_scoreboard.sv  
// =============================================================================
class axi4_scoreboard extends uvm_scoreboard;
    `uvm_component_utils(axi4_scoreboard)

    uvm_analysis_imp #(axi4_seq_item, axi4_scoreboard) analysis_export;

    logic [31:0] shadow_mem [bit [23:0]];
    logic [3:0]  byte_valid [bit [23:0]];

    int writes_checked = 0;
    int reads_checked  = 0;
    int errors         = 0;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        analysis_export = new("analysis_export", this);
    endfunction

    function void write(axi4_seq_item item);
        if (item.is_write) process_write(item);
        else               process_read(item);
    endfunction

    function void process_write(axi4_seq_item item);
        automatic bit [25:0] addr = item.addr;
        for (int i = 0; i < item.data.size(); i++) begin
            automatic bit [23:0] waddr = addr[25:2];
            if (!shadow_mem.exists(waddr)) begin
                shadow_mem[waddr] = 32'h0;
                byte_valid[waddr] = 4'h0;
            end
            for (int b = 0; b < 4; b++) begin
                if (item.strb[i][b]) begin
                    shadow_mem[waddr][b*8 +: 8] = item.data[i][b*8 +: 8];
                    byte_valid[waddr][b]         = 1'b1;
                end
            end
            if (item.burst_type == 2'b01) addr += 4;
        end
        writes_checked++;
    endfunction

    function void process_read(axi4_seq_item item);
        automatic bit [25:0] addr = item.addr;
        for (int i = 0; i < item.data.size(); i++) begin
            automatic bit [23:0] waddr = addr[25:2];
            if (shadow_mem.exists(waddr)) begin
                for (int b = 0; b < 4; b++) begin
                    if (byte_valid[waddr][b]) begin
                        if (item.data[i][b*8 +: 8] !==
                            shadow_mem[waddr][b*8 +: 8]) begin
                            `uvm_error("SCB",
                                $sformatf("MISMATCH addr=0x%07h beat=%0d byte=%0d: exp=0x%02h got=0x%02h",
                                    addr, i, b,
                                    shadow_mem[waddr][b*8 +: 8],
                                    item.data[i][b*8 +: 8]))
                            errors++;
                        end
                    end
                end
            end
            if (item.rresp[i] !== 2'b00)
                `uvm_error("SCB", $sformatf(
                    "RRESP!=OKAY addr=0x%07h beat=%0d rresp=%02b",
                    addr, i, item.rresp[i]))
            if (item.burst_type == 2'b01) addr += 4;
        end
        reads_checked++;
    endfunction

    function void report_phase(uvm_phase phase);
        `uvm_info("SCB", $sformatf(
            "=== SCOREBOARD: writes=%0d reads=%0d errors=%0d ===",
            writes_checked, reads_checked, errors), UVM_NONE)
        if (errors == 0)
            `uvm_info("SCB",  "TODOS LOS CHECKS PASARON", UVM_NONE)
        else
            `uvm_error("SCB", $sformatf("%0d ERROR(S) DETECTADOS", errors))
    endfunction

endclass : axi4_scoreboard