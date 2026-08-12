classdef test_performanceMargin < matlab.unittest.TestCase
    %test_performanceMargin computePerformanceMargin.m (SNR-to-target-PER)
    %computation, isolated from toolbox network-simulation runs.

    methods (Test)
        function returnsFiniteDBValueInPlausibleRange(testCase)
            runResult.scenario.perThreshold = 40;
            margin = computePerformanceMargin(runResult);
            testCase.verifyTrue(isfinite(margin));
            testCase.verifyGreaterThan(margin, -5);
            testCase.verifyLessThan(margin, 30);
        end

        function stricterThresholdRequiresHigherSNR(testCase)
            % A lower (stricter) PER threshold should require a higher Eb/N0.
            runResultLoose.scenario.perThreshold = 40;
            runResultStrict.scenario.perThreshold = 1;
            marginLoose = computePerformanceMargin(runResultLoose);
            marginStrict = computePerformanceMargin(runResultStrict);
            testCase.verifyGreaterThan(marginStrict, marginLoose);
        end
    end
end
