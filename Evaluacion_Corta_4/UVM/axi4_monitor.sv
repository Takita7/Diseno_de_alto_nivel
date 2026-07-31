// =============================================================================
// axi4_monitor.sv  
// =============================================================================
class axi4_monitor extends uvm_monitor;
    `uvm_component_utils(axi4_monitor)

    virtual axi4_if.MONITOR vif;
    uvm_analysis_port #(axi4_seq_item) ap;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        ap = new("ap", this);
        if (!uvm_config_db #(virtual axi4_if.MONITOR)::get(
                this, "", "vif", vif))
            `uvm_fatal("CFG", "axi4_monitor: no vif en config_db")
    endfunction

    task run_phase(uvm_phase phase);
        fork
            monitor_writes();
            monitor_reads();
        join
    endtask

    task monitor_writes();
        forever begin
            axi4_seq_item item;
            item = axi4_seq_item::type_id::create("mon_wr");
            item.is_write = 1;

            // Esperar handshake AW
            @(vif.monitor_cb);
            while (!(vif.monitor_cb.awvalid && vif.monitor_cb.awready))
                @(vif.monitor_cb);

            item.id         = vif.monitor_cb.awid;
            item.addr       = vif.monitor_cb.awaddr;
            item.burst_len  = vif.monitor_cb.awlen;
            item.burst_type = vif.monitor_cb.awburst;

            // Recolectar W beats
            begin
                automatic int beats = item.burst_len + 1;
                item.data = new[beats];
                item.strb = new[beats];
                for (int i = 0; i < beats; i++) begin
                    @(vif.monitor_cb);
                    while (!(vif.monitor_cb.wvalid && vif.monitor_cb.wready))
                        @(vif.monitor_cb);
                    item.data[i] = vif.monitor_cb.wdata;
                    item.strb[i] = vif.monitor_cb.wstrb;
                end
            end

            // Esperar respuesta B
            @(vif.monitor_cb);
            while (!(vif.monitor_cb.bvalid && vif.monitor_cb.bready))
                @(vif.monitor_cb);
            item.bresp = vif.monitor_cb.bresp;

            `uvm_info("MON", {"WR: ", item.convert2string()}, UVM_HIGH)
            ap.write(item);
        end
    endtask

    task monitor_reads();
        forever begin
            axi4_seq_item item;
            item = axi4_seq_item::type_id::create("mon_rd");
            item.is_write = 0;

            // Esperar handshake AR
            @(vif.monitor_cb);
            while (!(vif.monitor_cb.arvalid && vif.monitor_cb.arready))
                @(vif.monitor_cb);

            item.id         = vif.monitor_cb.arid;
            item.addr       = vif.monitor_cb.araddr;
            item.burst_len  = vif.monitor_cb.arlen;
            item.burst_type = vif.monitor_cb.arburst;

            // Recolectar R beats
            begin
                automatic int beats = item.burst_len + 1;
                item.data  = new[beats];
                item.rresp = new[beats];
                for (int i = 0; i < beats; i++) begin
                    @(vif.monitor_cb);
                    while (!(vif.monitor_cb.rvalid && vif.monitor_cb.rready))
                        @(vif.monitor_cb);
                    item.data[i]  = vif.monitor_cb.rdata;
                    item.rresp[i] = vif.monitor_cb.rresp;
                end
            end

            `uvm_info("MON", {"RD: ", item.convert2string()}, UVM_HIGH)
            ap.write(item);
        end
    endtask

endclass : axi4_monitor