// =============================================================================
// axi4_coverage.sv  -  Cobertura funcional AXI4
// =============================================================================
class axi4_coverage extends uvm_subscriber #(axi4_seq_item);
    `uvm_component_utils(axi4_coverage)

    axi4_seq_item item;

    covergroup axi4_cg;
        // Tipo de transaccion
        cp_type: coverpoint item.is_write {
            bins write = {1};
            bins read  = {0};
        }
        // Distribucion de burst length
        cp_burst: coverpoint item.burst_len {
            bins single  = {0};
            bins small   = {[1:3]};
            bins medium  = {[4:15]};
            bins large   = {[16:63]};
            bins max_len = {[64:255]};
        }
        // Region de memoria (bits [25:22] de la direccion)
        cp_region: coverpoint item.addr[25:22] {
            bins rgb_area  = {[4'h0:4'h5]};  // 0x000000 - 0x5FFFFF
            bins gray_area = {4'h6, 4'h7};   // 0x600000 - 0x7FFFFF
            bins other     = default;
        }
        // Patron de byte enables (primer beat)
        cp_strb: coverpoint item.strb[0] {
            bins full    = {4'hF};
            bins partial = {[4'h1:4'hE]};
        }
        // Cross tipo x burst length
        cx_type_burst: cross cp_type, cp_burst;
    endgroup

    function new(string name, uvm_component parent);
        super.new(name, parent);
        axi4_cg = new();
    endfunction

    function void write(axi4_seq_item t);
        item = t;
        axi4_cg.sample();
    endfunction

    function void report_phase(uvm_phase phase);
        `uvm_info("COV", $sformatf("Covertura total: %.1f%%",
            axi4_cg.get_coverage()), UVM_NONE)
    endfunction

endclass : axi4_coverage