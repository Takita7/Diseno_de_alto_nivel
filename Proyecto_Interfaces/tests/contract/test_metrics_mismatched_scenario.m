classdef test_metrics_mismatched_scenario < matlab.unittest.TestCase
    %test_metrics_mismatched_scenario summarizeCoexistenceMetrics.m rejects
    %two runs with mismatched scenario values (Contract 2 failure mode)

    methods (Test)
        function rejectsMismatchedScenario(testCase)
            runOff = runCoexistenceScenario('SimulationTime', 0.2, 'AFHEnabled', false);
            runOn = runCoexistenceScenario('SimulationTime', 0.2, 'AFHEnabled', true);
            runOn.scenario.perThreshold = runOn.scenario.perThreshold + 1;
            testCase.verifyError(@() summarizeCoexistenceMetrics(runOff, runOn), ...
                "summarizeCoexistenceMetrics:MismatchedScenario");
        end

        function rejectsSameAfhConditionOnBothRuns(testCase)
            runOff = runCoexistenceScenario('SimulationTime', 0.2, 'AFHEnabled', false);
            testCase.verifyError(@() summarizeCoexistenceMetrics(runOff, runOff), ...
                "summarizeCoexistenceMetrics:SameAfhCondition");
        end
    end
end
