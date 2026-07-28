// =============================================================================
// axi4_env.sv  -  Environment AXI4
// =============================================================================
class axi4_env extends uvm_env;
    `uvm_component_utils(axi4_env)

    axi4_agent       agent;
    axi4_scoreboard  scoreboard;
    axi4_coverage    coverage;

    function new(string name, uvm_component parent);
        super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
        super.build_phase(phase);
        agent      = axi4_agent     ::type_id::create("agent",      this);
        scoreboard = axi4_scoreboard::type_id::create("scoreboard", this);
        coverage   = axi4_coverage  ::type_id::create("coverage",   this);
    endfunction

    function void connect_phase(uvm_phase phase);
        agent.monitor.ap.connect(scoreboard.analysis_export);
        agent.monitor.ap.connect(coverage.analysis_export);
    endfunction

endclass : axi4_env