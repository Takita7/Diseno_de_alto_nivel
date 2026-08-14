classdef test_tx_bredr_contract < matlab.unittest.TestCase
    %test_tx_bredr_contract generateBREDRWaveform.m matches Contract 1's
    %BluetoothWaveform shape (contracts/stage-interfaces.md)

    methods (Test)
        function returnsRequiredFields(testCase)
            wfm = generateBREDRWaveform();
            requiredFields = ["mode", "phyVariant", "configObject", "samples", ...
                "sampleRate", "payloadBits", "sourceReference"];
            for f = requiredFields
                testCase.verifyTrue(isfield(wfm, f), "Missing field: " + f);
            end
        end

        function modeIsBREDR(testCase)
            wfm = generateBREDRWaveform();
            testCase.verifyEqual(string(wfm.mode), "BREDR");
        end

        function samplesAreNonEmpty(testCase)
            wfm = generateBREDRWaveform();
            testCase.verifyNotEmpty(wfm.samples);
        end

        function payloadBitsMatchPacketByteLength(testCase)
            wfm = generateBREDRWaveform();
            numBytes = getPayloadLength(wfm.configObject);
            testCase.verifyEqual(numel(wfm.payloadBits), numBytes * 8);
        end

        function sourceReferenceNamesToolboxFunctions(testCase)
            wfm = generateBREDRWaveform();
            testCase.verifySubstring(char(wfm.sourceReference), "bluetoothWaveformGenerator");
        end
    end
end
