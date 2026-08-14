classdef test_afh_comparison < matlab.unittest.TestCase
    %test_afh_comparison Comparing an AFH-off run against an AFH-on run of
    %the same scenario produces per.afhOff, per.afhOn, and a correctly
    %computed afhImprovementDelta (spec.md User Story 3, Acceptance
    %Scenario 2; quickstart.md Step 5).
    %
    %   Runs at the mandatory example's default 1.5 s simulated duration
    %   (twice) so AFH has enough classification intervals (250 ms each) to
    %   show a real effect — this test takes roughly 30-60 s.

    methods (Test)
        function afhReducesOrMaintainsPER(testCase)
            runOff = runCoexistenceScenario('AFHEnabled', false);
            runOn = runCoexistenceScenario('AFHEnabled', true);
            report = summarizeCoexistenceMetrics(runOff, runOn);

            testCase.verifyEqual(report.afhImprovementDelta, ...
                report.per.afhOff - report.per.afhOn, 'AbsTol', 1e-12);
            testCase.verifyGreaterThanOrEqual(report.afhImprovementDelta, 0, ...
                "AFH is expected to reduce or maintain PER, not worsen it (quickstart.md Step 5)");
        end
    end
end
