function wfm = generateBREDRWaveform(varargin)
%generateBREDRWaveform Generate a standard-conformant Bluetooth BR/EDR waveform
%   WFM = generateBREDRWaveform() generates a conformant BR/EDR waveform
%   (default PacketType "DH1", matching the ACL packet type used in
%   Matlab/main_simulation.m's bluetoothConnectionConfig) via
%   bluetoothWaveformGenerator/bluetoothWaveformConfig, and returns a
%   BluetoothWaveform struct (data-model.md). Satisfies FR-001/FR-003/FR-010.
%
%   WFM = generateBREDRWaveform(Name=Value) forwards Name=Value pairs to
%   bluetoothWaveformConfig (e.g. generateBREDRWaveform(Mode="EDR2M")),
%   overriding the DH1 default in the order given.

cfgWaveform = bluetoothWaveformConfig('PacketType', 'DH1');
for k = 1:2:numel(varargin)
    cfgWaveform.(varargin{k}) = varargin{k+1};
end

numPayloadBytes = getPayloadLength(cfgWaveform);
payloadBits = randi([0 1], numPayloadBytes * 8, 1);

samples = bluetoothWaveformGenerator(payloadBits, cfgWaveform);

% BR/EDR symbol rate is fixed at 1 Msym/s regardless of Mode (BR/EDR2M/EDR3M
% differ in bits/symbol, not symbol rate) — see toolbox/bluetooth/bluetooth/
% bluetoothWaveformGenerator.m header example ("symbolRate = 1e6").
sampleRate = cfgWaveform.SamplesPerSymbol * 1e6;

wfm = newBluetoothWaveform("BREDR", string(cfgWaveform.PacketType), cfgWaveform, ...
    samples, sampleRate, payloadBits, "bluetoothWaveformGenerator/bluetoothWaveformConfig");

end
