classdef test_bredr_conformance < matlab.unittest.TestCase
    %test_bredr_conformance Full BR/EDR Tx->Rx round trip yields a near-zero
    %BER (spec.md User Story 1, Acceptance Scenario 1)

    methods (Test)
        function fullRoundTripYieldsNearZeroBER(testCase)
            wfm = generateBREDRWaveform();
            testCase.verifyEqual(string(wfm.mode), "BREDR");
            result = measureLinkBER(wfm);
            testCase.verifyLessThanOrEqual(result.ber, 0.01);
            testCase.verifyTrue(result.pktValidStatus);
        end
    end
end
