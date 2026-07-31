// =============================================================================
// axi4_test.sv  -  Tests UVM para ram_axi4
//
// axi4_base_test  - base con env, raise/drop objection
// smoke_test      - wr_rd + strb (+UVM_TESTNAME=smoke_test)
// burst_test      - 32 beats     (+UVM_TESTNAME=burst_test)
// stress_test     - 20 txns      (+UVM_TESTNAME=stress_test)
// =============================================================================

// ---- Base test --------------------------------------------------------------
class axi4_base_test extends uvm_test;
    `uvm_component_utils(axi4_base_test)

    axi4_env env;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        env = axi4_env::type_id::create("env", this);
    endfunction

    task run_phase(uvm_phase phase);
        phase.raise_objection(this);
        run_sequences(phase);
        phase.drop_objection(this);
    endtask

    virtual task run_sequences(uvm_phase phase);
    endtask

endclass : axi4_base_test

// ---- smoke_test: write-read + byte enables ----------------------------------
class smoke_test extends axi4_base_test;
    `uvm_component_utils(smoke_test)
    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    task run_sequences(uvm_phase phase);
        wr_rd_seq seq1 = wr_rd_seq::type_id::create("seq1");
        strb_seq  seq2 = strb_seq ::type_id::create("seq2");
        `uvm_info("TEST", "=== smoke_test START ===", UVM_NONE)
        seq1.start(env.agent.sequencer);
        seq2.start(env.agent.sequencer);
        `uvm_info("TEST", "=== smoke_test END ===", UVM_NONE)
    endtask
endclass : smoke_test

// ---- burst_test: burst de 32 beats ------------------------------------------
class burst_test extends axi4_base_test;
    `uvm_component_utils(burst_test)
    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    task run_sequences(uvm_phase phase);
        burst_seq seq = burst_seq::type_id::create("seq");
        `uvm_info("TEST", "=== burst_test START ===", UVM_NONE)
        seq.num_beats = 32;
        seq.start(env.agent.sequencer);
        `uvm_info("TEST", "=== burst_test END ===", UVM_NONE)
    endtask
endclass : burst_test

// ---- stress_test: 20 transacciones aleatorias -------------------------------
class stress_test extends axi4_base_test;
    `uvm_component_utils(stress_test)
    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    task run_sequences(uvm_phase phase);
        stress_seq seq = stress_seq::type_id::create("seq");
        `uvm_info("TEST", "=== stress_test START ===", UVM_NONE)
        seq.num_txns = 20;
        seq.start(env.agent.sequencer);
        `uvm_info("TEST", "=== stress_test END ===", UVM_NONE)
    endtask
endclass : stress_test