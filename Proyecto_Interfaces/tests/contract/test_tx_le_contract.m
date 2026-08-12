classdef test_tx_le_contract < matlab.unittest.TestCase
    %test_tx_le_contract generateLEWaveform.m matches Contract 1's
    %BluetoothWaveform shape (contracts/stage-interfaces.md)

    methods (Test)
        function returnsRequiredFields(testCase)
            wfm = generateLEWaveform();
            requiredFields = ["mode", "phyVariant", "configObject", "samples", ...
                "sampleRate", "payloadBits", "sourceReference"];
            for f = requiredFields
                testCase.verifyTrue(isfield(wfm, f), "Missing field: " + f);
            end
        end

        function modeIsLE(testCase)
            wfm = generateLEWaveform();
            testCase.verifyEqual(string(wfm.mode), "LE");
        end

        function phyVariantIsAValidLEMode(testCase)
            wfm = generateLEWaveform();
            testCase.verifyTrue(ismember(wfm.phyVariant, ["LE1M", "LE2M", "LE500K", "LE125K"]));
        end

        function samplesAreNonEmpty(testCase)
            wfm = generateLEWaveform();
            testCase.verifyNotEmpty(wfm.samples);
        end

        function sourceReferenceNamesToolboxFunction(testCase)
            wfm = generateLEWaveform();
            testCase.verifySubstring(char(wfm.sourceReference), "bleWaveformGenerator");
        end
    end
end
