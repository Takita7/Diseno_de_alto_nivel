classdef test_le_conformance < matlab.unittest.TestCase
    %test_le_conformance Full LE Tx->Rx round trip yields a near-zero BER
    %(spec.md User Story 1, Acceptance Scenario 2)

    methods (Test)
        function fullRoundTripYieldsNearZeroBER(testCase)
            wfm = generateLEWaveform();
            testCase.verifyEqual(string(wfm.mode), "LE");
            result = measureLinkBER(wfm);
            testCase.verifyLessThanOrEqual(result.ber, 0.01);
            testCase.verifyTrue(result.pktValidStatus);
        end
    end
end
