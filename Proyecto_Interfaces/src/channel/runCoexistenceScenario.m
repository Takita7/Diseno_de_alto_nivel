function runResult = runCoexistenceScenario(varargin)
%runCoexistenceScenario Run the mandatory BR/EDR + WLAN coexistence scenario
%   RUNRESULT = runCoexistenceScenario() runs the scenario from
%   scenarioConfig() (FR-004/FR-005) with AFH enabled, WLAN interference
%   enabled, for the mandatory example's default 1.5 s simulated duration,
%   and returns a Contract 2 struct:
%     scenario       - the ChannelScenario struct this run used
%     afhEnabled     - logical, which of the two AFH conditions this run is
%     perPacketLog   - table of PacketReceptionEnded records (Time,
%                      ChannelIndex, SuccessStatus, SourceNodeID)
%     simDurationSec - simulated duration in seconds
%
%   RUNRESULT = runCoexistenceScenario('AFHEnabled', tf) toggles adaptive
%   frequency hopping (channel classification) on or off — this is what lets
%   User Story 3 compare the two conditions under FR-007 by calling this
%   function twice with the same scenario.
%
%   RUNRESULT = runCoexistenceScenario(..., 'EnableWLANInterference', tf)
%   toggles the WLAN interferer (default true).
%
%   RUNRESULT = runCoexistenceScenario(..., 'EnableVisualization', tf) shows
%   the live helperVisualizeCoexistence figure (default false — off for
%   automated/test runs, since it blocks on pause() per packet).
%
%   RUNRESULT = runCoexistenceScenario(..., 'SimulationTime', t) overrides
%   the default 1.5 s simulated duration.
%
%   RUNRESULT = runCoexistenceScenario(..., 'Seed', s) overrides the default
%   RNG seed (1) used for reproducibility.
%
%   This is Matlab/main_simulation.m refactored into a callable function per
%   plan.md's Project Structure, sourcing every channel/interference
%   parameter from scenarioConfig.m (FR-005) instead of inline literals.

opts = struct( ...
    'AFHEnabled', true, ...
    'EnableWLANInterference', true, ...
    'EnableVisualization', false, ...
    'SimulationTime', 1.5, ...
    'Seed', 1);
for k = 1:2:numel(varargin)
    opts.(varargin{k}) = varargin{k+1};
end

scenario = scenarioConfig();
rng(opts.Seed, "twister");

networkSimulator = wirelessNetworkSimulator.init;

centralNode = bluetoothNode("central", Name="Central Node", Position=[0 0 0]);
peripheralNode = bluetoothNode("peripheral", Name="Peripheral Node", Position=[5 0 0]);

connectionConfig = bluetoothConnectionConfig;
connectionConfig.CentralToPeripheralACLPacketType = "DH1";
connectionConfig.PeripheralToCentralACLPacketType = "DH1";
connectionConfig.SCOPacketType = "HV2";
connectionConfig.PollInterval = 10;
connectionConfig.InstantOffset = 96;
connectionConfig.TransmitterPower = 0;
configureConnection(connectionConfig, centralNode, peripheralNode);

trafficSource = networkTrafficOnOff(OnTime=Inf, DataRate=200, PacketSize=27);
addTrafficSource(centralNode, trafficSource, DestinationNode=peripheralNode);
addTrafficSource(peripheralNode, trafficSource, DestinationNode=centralNode);

wlanNodes = helperInterferingWLANNode.empty(0, 0);
if opts.EnableWLANInterference
    bbFileName = 'WLANHESUBandwidth20.bb';
    bbFilePath = which(bbFileName);
    if isempty(bbFilePath)
        error("runCoexistenceScenario:MissingBasebandFile", ...
            "%s not found on the MATLAB path. Add Matlab/ to the path (see startup.m).", bbFileName);
    end

    interference = scenario.interferenceSource;
    numWLANNodes = numel(interference.centerFrequency);
    wlanNodePositions = [0 7 5; 0 3 0];
    wlanNodes = helperInterferingWLANNode.empty(0, numWLANNodes);
    for idx = 1:numWLANNodes
        wlanNodes(idx) = helperInterferingWLANNode( ...
            WaveformSource="BasebandFile", ...
            BasebandFile=bbFilePath, ...
            Position=wlanNodePositions(idx, :), ...
            Name="WLAN node", ...
            TransmitterPower=20, ...
            CenterFrequency=interference.centerFrequency(idx), ...
            Bandwidth=interference.bandwidth, ...
            SignalPeriodicity=interference.periodicity);
    end
end

bluetoothNodes = [centralNode peripheralNode];
addNodes(networkSimulator, bluetoothNodes);
if opts.EnableWLANInterference
    addNodes(networkSimulator, wlanNodes);
end

if opts.AFHEnabled
    classifierObj = helperBluetoothChannelClassification(centralNode, peripheralNode, ...
        PERThreshold=scenario.perThreshold);
    classifyFcn = @(varargin) classifierObj.classifyChannels;
    scheduleAction(networkSimulator, classifyFcn, [], 0, scenario.classificationIntervalMs / 1000);
end

logTime = zeros(0, 1);
logChannel = zeros(0, 1);
logSuccess = false(0, 1);
logSource = zeros(0, 1);
addlistener(centralNode, "PacketReceptionEnded", @recordReception);

if opts.EnableVisualization
    if opts.EnableWLANInterference
        helperVisualizeCoexistence(opts.SimulationTime, bluetoothNodes, wlanNodes);
    else
        helperVisualizeCoexistence(opts.SimulationTime, bluetoothNodes);
    end
end

run(networkSimulator, opts.SimulationTime);

perPacketLog = table(logTime, logChannel, logSuccess, logSource, ...
    'VariableNames', {'Time', 'ChannelIndex', 'SuccessStatus', 'SourceNodeID'});

runResult = struct( ...
    'scenario', scenario, ...
    'afhEnabled', logical(opts.AFHEnabled), ...
    'perPacketLog', perPacketLog, ...
    'simDurationSec', opts.SimulationTime);

    function recordReception(~, eventdata)
        %recordReception Append one PacketReceptionEnded record to the log
        info = eventdata.Data;
        logTime(end + 1, 1) = info.CurrentTime;
        logChannel(end + 1, 1) = info.ChannelIndex;
        logSuccess(end + 1, 1) = logical(info.SuccessStatus);
        logSource(end + 1, 1) = info.SourceNodeID;
    end

end
