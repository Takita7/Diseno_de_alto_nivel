function ebNoRequiredDB = computePerformanceMargin(runResult)
%computePerformanceMargin SNR (Eb/N0) required to reach the target PER, in dB
%   EBNOREQUIREDDB = computePerformanceMargin(RUNRESULT) returns the Eb/N0
%   (dB) an AWGN link would need to meet RUNRESULT.scenario.perThreshold,
%   satisfying FR-008 normalized metric #1 ("SNR required to reach a target
%   BER", data-model.md).
%
%   Method: this is a closed-form analytical estimate, not a simulated
%   sweep (FR-004 rules out sweeping simulation scenarios) — it converts the
%   scenario's packet-level PER threshold to an equivalent per-bit BER
%   target (using the packet size computeSpectralEfficiency.m also uses),
%   then finds the smallest Eb/N0 for which the Communications Toolbox's
%   berawgn (non-coherent binary FSK — the standard textbook approximation
%   for Bluetooth's GFSK) predicts a BER at or below that target. Report
%   this margin in the video as an analytical estimate, not a directly
%   measured/simulated number.

PAYLOAD_BITS_PER_PACKET = 27 * 8; % matches computeSpectralEfficiency.m
EBNO_SEARCH_RANGE_DB = -5:0.1:30;

targetPER = runResult.scenario.perThreshold / 100; % perThreshold is stored as a percent [1,100]
targetBER = 1 - (1 - targetPER)^(1 / PAYLOAD_BITS_PER_PACKET);

berCurve = berawgn(EBNO_SEARCH_RANGE_DB, 'fsk', 2, 'noncoherent');
idx = find(berCurve <= targetBER, 1, 'first');

if isempty(idx)
    error("computePerformanceMargin:NoSolutionInRange", ...
        "No Eb/N0 in [%g, %g] dB reaches the target BER %.6g; widen EBNO_SEARCH_RANGE_DB.", ...
        EBNO_SEARCH_RANGE_DB(1), EBNO_SEARCH_RANGE_DB(end), targetBER);
end

ebNoRequiredDB = EBNO_SEARCH_RANGE_DB(idx);

end
