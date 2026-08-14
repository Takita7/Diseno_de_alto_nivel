function seBitsPerHz = computeSpectralEfficiency(runResult)
%computeSpectralEfficiency Bits/s/Hz achieved by a coexistence run
%   SEBITSPERHZ = computeSpectralEfficiency(RUNRESULT) computes achieved
%   throughput (successful packets x payload bits, divided by the simulated
%   duration) divided by the standard Bluetooth channel bandwidth, per
%   research.md Decision 4 (FR-008 normalized metric #4). No dedicated
%   toolbox "spectral efficiency" function exists, so this is a direct
%   derived quantity from data runCoexistenceScenario already produces.
%
%   RUNRESULT is a Contract 2 struct (see runCoexistenceScenario.m) with a
%   non-empty perPacketLog and a simDurationSec.

PAYLOAD_BITS_PER_PACKET = 27 * 8; % matches PacketSize=27 bytes in runCoexistenceScenario's trafficSource
BLUETOOTH_CHANNEL_BANDWIDTH_HZ = 1e6; % standard 1 MHz BR/EDR & LE1M/LE2M-class channel spacing

if isempty(runResult.perPacketLog)
    error("computeSpectralEfficiency:EmptyLog", ...
        "runResult.perPacketLog must be non-empty.");
end

numSuccessful = sum(runResult.perPacketLog.SuccessStatus);
throughputBitsPerSec = (numSuccessful * PAYLOAD_BITS_PER_PACKET) / runResult.simDurationSec;
seBitsPerHz = throughputBitsPerSec / BLUETOOTH_CHANNEL_BANDWIDTH_HZ;

end
