classdef test_coexistence_baseline < matlab.unittest.TestCase
    %test_coexistence_baseline Running the scenario (AFH disabled) produces
    %a non-empty perPacketLog (spec.md User Story 2, quickstart.md Step 4)

    methods (Test)
        function afhDisabledRunProducesNonEmptyLog(testCase)
            runOff = runCoexistenceScenario('AFHEnabled', false, 'SimulationTime', 0.3);
            testCase.verifyNotEmpty(runOff.perPacketLog);
            testCase.verifyFalse(runOff.afhEnabled);
        end
    end
end
