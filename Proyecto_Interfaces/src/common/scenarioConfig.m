function scenario = scenarioConfig()
%scenarioConfig Return the mandatory Bluetooth/WLAN coexistence ChannelScenario
%   SCENARIO = scenarioConfig() returns the single representative 2.4 GHz
%   Bluetooth BR/EDR + WLAN coexistence scenario (FR-004/FR-005 in
%   docs/001-bluetooth-link-simulation/spec.md). Every numeric value below is
%   taken unmodified from MathWorks' official "Bluetooth BR Data and Voice
%   Communication with WLAN Signal Interference" example (source PDF
%   2026_2C_IC_AE.pdf, Sec. 11 item 4) as already used in Matlab/main_simulation.m
%   — this function only centralizes those values so every caller (channel,
%   tests) shares one cited source instead of re-typing them inline.
%
%   Deviation from data-model.md: `afhEnabled` is intentionally NOT a field of
%   this struct. It lives on runCoexistenceScenario's output wrapper instead
%   (per contracts/stage-interfaces.md Contract 2), because FR-007 requires
%   comparing two runs of the *same* scenario with AFH off vs. on — folding
%   the toggle into ChannelScenario itself would make the two runs look like
%   different scenarios and break that comparison.

scenario = struct();
scenario.scenarioName = "BluetoothBREDR_WLANCoexistence";

scenario.interferenceSource = struct( ...
    'type', "WLAN HE-SU", ...
    'centerFrequency', [2.442e9, 2.447e9], ... % Hz, WLAN channels 7 and 8
    'bandwidth', 20e6, ...                      % Hz
    'periodicity', 2e-3);                       % s

scenario.classificationIntervalMs = 250;
scenario.perThreshold = 40; % percent, per helperBluetoothChannelClassification.PERThreshold range [1,100]

scenario.citationSource = struct( ...
    'interferenceSource', "MathWorks 'Bluetooth BR Data and Voice Communication with WLAN Signal Interference' example (source PDF Sec. 11 item 4): two WLAN HE-SU interferers on channels 7/8 (2.442/2.447 GHz), 20 MHz bandwidth, 2 ms signal periodicity.", ...
    'classificationIntervalMs', "Same example: AFH channel classification is re-evaluated every 250 ms (scheduleAction periodicity in main_simulation.m).", ...
    'perThreshold', "Same example: helperBluetoothChannelClassification default PERThreshold = 40%.");

end
