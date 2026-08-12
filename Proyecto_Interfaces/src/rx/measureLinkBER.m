function result = measureLinkBER(wfm)
%measureLinkBER Link-level BER check for a BluetoothWaveform (Contract 1)
%   RESULT = measureLinkBER(WFM) decodes WFM (a BluetoothWaveform struct
%   returned by generateBREDRWaveform/generateLEWaveform, see data-model.md)
%   with the mode-appropriate ideal receiver, compares the decoded bits
%   against the known WFM.payloadBits, and returns a struct with fields:
%     ber            - bit error rate in [0, 1]
%     pktValidStatus - logical, whether the packet decoded to a valid,
%                      correctly-sized payload
%   Satisfies FR-006 and contracts/stage-interfaces.md Contract 1. Only uses
%   the fields Contract 1 guarantees (mode, phyVariant, configObject,
%   samples, sampleRate, payloadBits, sourceReference), so either Tx function
%   can be swapped independently.
%
%   Per Contract 1's failure mode: raises an error (rather than silently
%   returning ber = 0) when mode is neither "BREDR" nor "LE", or when
%   samples is empty — a silent zero would be indistinguishable from a
%   genuinely perfect link.

if ~isfield(wfm, 'mode') || ~ismember(string(wfm.mode), ["BREDR", "LE"])
    error("measureLinkBER:InvalidMode", ...
        'wfm.mode must be "BREDR" or "LE".');
end
if ~isfield(wfm, 'samples') || isempty(wfm.samples)
    error("measureLinkBER:EmptySamples", ...
        "wfm.samples must be non-empty.");
end

txBits = double(wfm.payloadBits(:));

switch string(wfm.mode)
    case "BREDR"
        cfgPHY = getPhyConfigProperties(wfm.configObject);
        [rxBits, ~, pktValidStatus] = bluetoothIdealReceiver(wfm.samples, cfgPHY);
        rxBits = double(rxBits(:));
    case "LE"
        cfg = wfm.configObject;
        [rxBits, ~] = bleIdealReceiver(wfm.samples, 'Mode', cfg.Mode, ...
            'SamplesPerSymbol', cfg.SamplesPerSymbol);
        rxBits = double(rxBits(:));
        % bleIdealReceiver has no validity flag of its own; a correctly
        % sized decode is the closest available proxy.
        pktValidStatus = numel(rxBits) == numel(txBits);
end

if numel(rxBits) == numel(txBits)
    ber = sum(rxBits ~= txBits) / numel(txBits);
else
    % Length mismatch means the receiver did not recover a payload the
    % right size to compare bit-for-bit — treat as a fully failed decode
    % rather than erroring, since this is an expected outcome once channel
    % impairment is introduced (not just in the ideal noiseless case).
    ber = 1;
    pktValidStatus = false;
end

result = struct('ber', ber, 'pktValidStatus', logical(pktValidStatus));

end
