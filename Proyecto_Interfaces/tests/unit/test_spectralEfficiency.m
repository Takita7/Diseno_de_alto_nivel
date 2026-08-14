classdef test_spectralEfficiency < matlab.unittest.TestCase
    %test_spectralEfficiency computeSpectralEfficiency.m matches
    %throughput / occupied bandwidth (research.md Decision 4). Uses a
    %synthetic runResult, isolated from toolbox network-simulation runs.

    methods (Test)
        function matchesThroughputDividedByBandwidth(testCase)
            successStatus = [true; true; false; true]; % 3 of 4 packets succeed
            runResult.perPacketLog = table(zeros(4, 1), zeros(4, 1), successStatus, zeros(4, 1), ...
                'VariableNames', {'Time', 'ChannelIndex', 'SuccessStatus', 'SourceNodeID'});
            runResult.simDurationSec = 1.0;

            se = computeSpectralEfficiency(runResult);

            expectedThroughputBitsPerSec = 3 * (27 * 8) / 1.0;
            expectedSE = expectedThroughputBitsPerSec / 1e6;
            testCase.verifyEqual(se, expectedSE, 'AbsTol', 1e-9);
        end

        function zeroSuccessfulPacketsGivesZeroEfficiency(testCase)
            runResult.perPacketLog = table(0, 0, false, 0, ...
                'VariableNames', {'Time', 'ChannelIndex', 'SuccessStatus', 'SourceNodeID'});
            runResult.simDurationSec = 1.0;
            testCase.verifyEqual(computeSpectralEfficiency(runResult), 0);
        end

        function rejectsEmptyLog(testCase)
            runResult.perPacketLog = table();
            runResult.simDurationSec = 1.0;
            testCase.verifyError(@() computeSpectralEfficiency(runResult), ...
                "computeSpectralEfficiency:EmptyLog");
        end
    end
end
