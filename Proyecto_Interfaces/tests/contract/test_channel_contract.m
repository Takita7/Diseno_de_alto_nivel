classdef test_channel_contract < matlab.unittest.TestCase
    %test_channel_contract runCoexistenceScenario.m matches Contract 2's
    %input-struct shape (contracts/stage-interfaces.md)

    methods (Test)
        function returnsContract2Shape(testCase)
            runResult = runCoexistenceScenario('SimulationTime', 0.2, 'AFHEnabled', false);
            testCase.verifyTrue(isfield(runResult, 'scenario'));
            testCase.verifyTrue(isfield(runResult, 'afhEnabled'));
            testCase.verifyTrue(isfield(runResult, 'perPacketLog'));
            testCase.verifyTrue(isfield(runResult, 'simDurationSec'));
            testCase.verifyClass(runResult.perPacketLog, 'table');
            testCase.verifyEqual(runResult.simDurationSec, 0.2);
            testCase.verifyFalse(runResult.afhEnabled);
        end

        function perPacketLogHasExpectedColumns(testCase)
            runResult = runCoexistenceScenario('SimulationTime', 0.2, 'AFHEnabled', false);
            testCase.verifyEqual(runResult.perPacketLog.Properties.VariableNames, ...
                {'Time', 'ChannelIndex', 'SuccessStatus', 'SourceNodeID'});
        end
    end
end
