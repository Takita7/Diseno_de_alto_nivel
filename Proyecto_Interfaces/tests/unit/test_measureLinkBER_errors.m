classdef test_measureLinkBER_errors < matlab.unittest.TestCase
    %test_measureLinkBER_errors measureLinkBER.m raises an error on an
    %invalid mode or empty samples, per Contract 1's failure mode

    methods (Test)
        function rejectsInvalidMode(testCase)
            wfm = struct('mode', 'BOGUS', 'samples', [1 2 3]);
            testCase.verifyError(@() measureLinkBER(wfm), "measureLinkBER:InvalidMode");
        end

        function rejectsEmptySamples(testCase)
            wfm = struct('mode', 'BREDR', 'samples', []);
            testCase.verifyError(@() measureLinkBER(wfm), "measureLinkBER:EmptySamples");
        end

        function rejectsMissingModeField(testCase)
            wfm = struct('samples', [1 2 3]);
            testCase.verifyError(@() measureLinkBER(wfm), "measureLinkBER:InvalidMode");
        end
    end
end
