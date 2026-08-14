function wfm = generateLEWaveform(varargin)
%generateLEWaveform Generate a standard-conformant Bluetooth LE waveform
%   WFM = generateLEWaveform() generates a conformant LE1M waveform via
%   bleWaveformGenerator, and returns a BluetoothWaveform struct
%   (data-model.md). Satisfies FR-002/FR-010 and research.md Decision 3: the
%   mandatory official coexistence example (source PDF Sec. 11 item 4) only
%   models BR/EDR, so LE is generated through the toolbox's own LE-specific
%   function instead of being hand-built.
%
%   WFM = generateLEWaveform(Name=Value) forwards Name=Value pairs to
%   bleWaveformGenerator (e.g. generateLEWaveform(Mode="LE2M")).

opts = struct('Mode', "LE1M", 'SamplesPerSymbol', 8);
for k = 1:2:numel(varargin)
    opts.(varargin{k}) = varargin{k+1};
end

numPayloadBytes = 27; % matches the ACL payload size used elsewhere in this project (main_simulation.m)
payloadBits = randi([0 1], numPayloadBytes * 8, 1);

nvArgs = namedargs2cell(opts);
samples = bleWaveformGenerator(payloadBits, nvArgs{:});

% LE1M/LE500K/LE125K use a 1 Msym/s symbol rate; LE2M uses 2 Msym/s
% (ble.internal.samplesPerSymbol).
symbolRate = 1e6;
if string(opts.Mode) == "LE2M"
    symbolRate = 2e6;
end
sampleRate = opts.SamplesPerSymbol * symbolRate;

wfm = newBluetoothWaveform("LE", string(opts.Mode), opts, samples, sampleRate, ...
    payloadBits, "bleWaveformGenerator");

end
