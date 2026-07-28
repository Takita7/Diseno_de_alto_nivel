// =============================================================================
// axi4_seq_item.sv  -  Transaction object AXI4
// =============================================================================
class axi4_seq_item extends uvm_sequence_item;
    `uvm_object_utils(axi4_seq_item)

    rand bit           is_write;
    rand bit [3:0]     id;
    rand bit [25:0]    addr;
    rand bit [7:0]     burst_len;
    rand bit [1:0]     burst_type;
    rand bit [31:0]    data [];
    rand bit [3:0]     strb [];

    // Respuesta (llenado por driver/monitor, no randomizado)
    bit [1:0]          bresp;
    bit [1:0]          rresp [];

    // ---- Constraints --------------------------------------------------------
    constraint addr_align_c  { addr[1:0] == 2'b00; }
    constraint burst_type_c  { burst_type == 2'b01; }
    constraint burst_len_c   { burst_len inside {[0:255]}; }
    constraint data_size_c   {
        data.size() == burst_len + 1;
        strb.size() == burst_len + 1;
    }
    constraint strb_valid_c  { foreach (strb[i]) strb[i] != 4'h0; }

    function new(string name = "axi4_seq_item");
        super.new(name);
    endfunction

    function string convert2string();
        return $sformatf("[%s] id=%0d addr=0x%07h beats=%0d",
            is_write ? "WR" : "RD", id, addr, burst_len+1);
    endfunction

    function void do_copy(uvm_object rhs);
        axi4_seq_item rhs_;
        if (!$cast(rhs_, rhs))
            `uvm_fatal("CAST", "axi4_seq_item: do_copy cast failed")
        super.do_copy(rhs);
        is_write   = rhs_.is_write;
        id         = rhs_.id;
        addr       = rhs_.addr;
        burst_len  = rhs_.burst_len;
        burst_type = rhs_.burst_type;
        data       = rhs_.data;
        strb       = rhs_.strb;
        bresp      = rhs_.bresp;
        rresp      = rhs_.rresp;
    endfunction

    function bit do_compare(uvm_object rhs, uvm_comparer comparer);
        axi4_seq_item rhs_;
        if (!$cast(rhs_, rhs)) return 0;
        return (super.do_compare(rhs, comparer) &&
                is_write  == rhs_.is_write &&
                id        == rhs_.id       &&
                addr      == rhs_.addr     &&
                burst_len == rhs_.burst_len);
    endfunction

endclass : axi4_seq_item