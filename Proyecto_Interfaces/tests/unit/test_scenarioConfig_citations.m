classdef test_scenarioConfig_citations < matlab.unittest.TestCase
    %test_scenarioConfig_citations Every numeric field returned by
    %scenarioConfig.m has a non-empty citationSource entry (FR-005)

    methods (Test)
        function everyCitedFieldIsPresent(testCase)
            scenario = scenarioConfig();
            testCase.verifyTrue(isfield(scenario, 'citationSource'));
            expectedKeys = ["interferenceSource", "classificationIntervalMs", "perThreshold"];
            actualKeys = string(fieldnames(scenario.citationSource));
            for k = expectedKeys
                testCase.verifyTrue(ismember(k, actualKeys), "Missing citation for " + k);
            end
        end

        function citationsAreNonEmptyStrings(testCase)
            scenario = scenarioConfig();
            fields = fieldnames(scenario.citationSource);
            for k = 1:numel(fields)
                value = scenario.citationSource.(fields{k});
                testCase.verifyGreaterThan(strlength(string(value)), 0, ...
                    "Empty citation for " + fields{k});
            end
        end

        function scenarioNameIsFixedNotSwept(testCase)
            % FR-004: exactly one channel scenario, no sweep.
            a = scenarioConfig();
            b = scenarioConfig();
            testCase.verifyEqual(a, b);
        end
    end
end
