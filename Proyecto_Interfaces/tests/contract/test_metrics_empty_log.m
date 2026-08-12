classdef test_metrics_empty_log < matlab.unittest.TestCase
    %test_metrics_empty_log summarizeCoexistenceMetrics.m raises an error on
    %an empty perPacketLog rather than reporting per = 0 (Contract 2 failure mode)

    methods (Test)
        function rejectsEmptyLogOnEitherSide(testCase)
            runOff = runCoexistenceScenario('SimulationTime', 0.2, 'AFHEnabled', false);
            runOn = runCoexistenceScenario('SimulationTime', 0.2, 'AFHEnabled', true);

            emptyOn = runOn;
            emptyOn.perPacketLog = runOn.perPacketLog([], :);
            testCase.verifyError(@() summarizeCoexistenceMetrics(runOff, emptyOn), ...
                "summarizeCoexistenceMetrics:EmptyLog");

            emptyOff = runOff;
            emptyOff.perPacketLog = runOff.perPacketLog([], :);
            testCase.verifyError(@() summarizeCoexistenceMetrics(emptyOff, runOn), ...
                "summarizeCoexistenceMetrics:EmptyLog");
        end
    end
end
