// =============================================================================
// axi4_driver.sv  -  Driver AXI4 Full
// =============================================================================
class axi4_driver extends uvm_driver #(axi4_seq_item);
    `uvm_component_utils(axi4_driver)

    virtual axi4_if.DRIVER vif;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        if (!uvm_config_db #(virtual axi4_if.DRIVER)::get(
                this, "", "vif", vif))
            `uvm_fatal("CFG", "axi4_driver: no vif en config_db")
    endfunction

    task run_phase(uvm_phase phase);
        axi4_seq_item item;
        reset_signals();
        @(posedge vif.clk iff vif.rst);
        repeat(2) @(vif.driver_cb);

        forever begin
            seq_item_port.get_next_item(item);
            `uvm_info("DRV", item.convert2string(), UVM_MEDIUM)
            if (item.is_write) drive_write(item);
            else               drive_read(item);
            seq_item_port.item_done();
        end
    endtask

    task reset_signals();
        vif.driver_cb.awvalid <= 0; vif.driver_cb.awid    <= 0;
        vif.driver_cb.awaddr  <= 0; vif.driver_cb.awlen   <= 0;
        vif.driver_cb.awsize  <= 0; vif.driver_cb.awburst <= 0;
        vif.driver_cb.wvalid  <= 0; vif.driver_cb.wdata   <= 0;
        vif.driver_cb.wstrb   <= 0; vif.driver_cb.wlast   <= 0;
        vif.driver_cb.bready  <= 0;
        vif.driver_cb.arvalid <= 0; vif.driver_cb.arid    <= 0;
        vif.driver_cb.araddr  <= 0; vif.driver_cb.arlen   <= 0;
        vif.driver_cb.arsize  <= 0; vif.driver_cb.arburst <= 0;
        vif.driver_cb.rready  <= 0;
    endtask

    // ---- Write: AW -> W beats (pipelined) -> B ------------------------------
    task drive_write(axi4_seq_item item);
        automatic int beats = item.data.size();

        // Write Address
        @(vif.driver_cb);
        vif.driver_cb.awid    <= item.id;
        vif.driver_cb.awaddr  <= item.addr;
        vif.driver_cb.awlen   <= item.burst_len;
        vif.driver_cb.awsize  <= 3'h2;
        vif.driver_cb.awburst <= item.burst_type;
        vif.driver_cb.awvalid <= 1;

        @(vif.driver_cb);
        while (!vif.driver_cb.awready) @(vif.driver_cb);
        vif.driver_cb.awvalid <= 0;

        // Write Data - pre-drive beat 0 antes del loop
        @(vif.driver_cb);
        vif.driver_cb.wdata  <= item.data[0];
        vif.driver_cb.wstrb  <= item.strb[0];
        vif.driver_cb.wlast  <= (beats == 1);
        vif.driver_cb.wvalid <= 1;

        for (int i = 0; i < beats; i++) begin
            @(vif.driver_cb);
            while (!vif.driver_cb.wready) @(vif.driver_cb);

            if (i < beats - 1) begin
                vif.driver_cb.wdata <= item.data[i+1];
                vif.driver_cb.wstrb <= item.strb[i+1];
                vif.driver_cb.wlast <= (i+1 == beats-1);
            end else begin
                vif.driver_cb.wvalid <= 0;
                vif.driver_cb.wlast  <= 0;
            end
        end

        // Write Response
        vif.driver_cb.bready <= 1;
        @(vif.driver_cb);
        while (!vif.driver_cb.bvalid) @(vif.driver_cb);
        item.bresp = vif.driver_cb.bresp;
        vif.driver_cb.bready <= 0;
    endtask

    // ---- Read: AR -> R beats ------------------------------------------------
    task drive_read(axi4_seq_item item);
        automatic int beats = item.burst_len + 1;

        // Read Address
        @(vif.driver_cb);
        vif.driver_cb.arid    <= item.id;
        vif.driver_cb.araddr  <= item.addr;
        vif.driver_cb.arlen   <= item.burst_len;
        vif.driver_cb.arsize  <= 3'h2;
        vif.driver_cb.arburst <= item.burst_type;
        vif.driver_cb.arvalid <= 1;

        @(vif.driver_cb);
        while (!vif.driver_cb.arready) @(vif.driver_cb);
        vif.driver_cb.arvalid <= 0;

        // Read Data
        item.data  = new[beats];
        item.rresp = new[beats];
        vif.driver_cb.rready <= 1;

        for (int i = 0; i < beats; i++) begin
            @(vif.driver_cb);
            while (!vif.driver_cb.rvalid) @(vif.driver_cb);
            item.data[i]  = vif.driver_cb.rdata;
            item.rresp[i] = vif.driver_cb.rresp;
        end
        vif.driver_cb.rready <= 0;
    endtask

endclass : axi4_driver