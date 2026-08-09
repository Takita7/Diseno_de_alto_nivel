classdef helperInterferingWLANNode < wirelessnetwork.internal.wirelessNode
    %helperInterferingWLANNode Create an object for a WLAN node that
    %generates WLAN signals that act as interference
    %   WLANNODE = helperInterferingWLANNode creates an object for WLAN
    %   node that generates a signal periodically to introduce WLAN
    %   interference. The node does not do protocol modeling and only
    %   transmits PHY signals (IQ samples) based on the configuration.
    %
    %   WLANNODE = helperInterferingWLANNode(Name=Value) creates an object
    %   for WLAN node with the specified property Name set to the specified
    %   Value. You can specify additional name-value pair arguments in any
    %   order as (Name1=Value1, ..., NameN=ValueN).
    %
    %   helperInterferingWLANNode properties:
    %
    %   ID                - Node identifier
    %   Position          - Node position
    %   Name              - Node name
    %   WaveformSource    - Source of WLAN waveform
    %   BasebandFile      - Baseband file path
    %   CenterFrequency   - Center frequency of signal in Hz
    %   FormatConfig      - Packet format configuration
    %   Bandwidth         - Bandwidth of the signal in Hz
    %   SignalPeriodicity - Periodicity of signals in seconds
    %   TransmitterPower  - Signal transmission power in dBm

    %   Copyright 2021-2023 The MathWorks, Inc.

    properties
        %WaveformSource Source of WLAN waveform
        %   Specify the waveform source as a character vector or string
        %   scalar having value as 'Generated' or 'BasebandFile'. If the
        %   value is 'Generated', the WLAN waveform is generated using
        %   features of the WLAN Toolbox(TM). If the value is
        %   'BasebandFile', the waveform is extracted from a .bb file. The
        %   default value is 'BasebandFile'.
        WaveformSource = 'BasebandFile'

        %BasebandFile Baseband file path
        %   Specify the baseband file path as a character vector or string
        %   scalar. This property specifies the baseband file location path
        %   from which the WLAN waveform is to be extracted. It is
        %   applicable when the waveform source is set as 'BasebandFile'.
        %   The default value is 'WLANHESUBandwidth20.bb'.
        BasebandFile = 'WLANHESUBandwidth20.bb'

        %CenterFrequency Center frequency of signal in Hz
        %   Specify the center frequency as a positive double value in Hz.
        %   This property specifies the center frequency of the generated
        %   WLAN waveform. The default value is 2.442e9 Hz.
        CenterFrequency (1,1) {mustBePositive} = 2.442e9

        %FormatConfig Packet format configuration
        %   Specify the packet format configuration object of type 
        %   <a href="matlab:help('wlanHESUConfig')">wlanHESUConfig</a>, <a href="matlab:help('wlanHETBConfig')">wlanHETBConfig</a>, <a href="matlab:help('wlanHTConfig')">wlanHTConfig</a>, <a href="matlab:help('wlanNonHTConfig')">wlanNonHTConfig</a>. 
        %   The format of the generated waveform is determined by the type
        %   of object. The default value is object of type wlanHESUConfig.
        FormatConfig = wlanHESUConfig

        %Bandwidth Bandwidth of the signal in Hz
        %   Specify the channel bandwidth of the signal in Hz. The default
        %   is 20e6 Hz.
        Bandwidth (1,1) {mustBePositive, mustBeFinite} = 20e6

        %SignalPeriodicity Periodicity of signals in seconds
        %   Specify the signal periodicity as a positive double value in
        %   seconds. This property specifies the interval between the start
        %   of two successive signals. The default value is 2e-3 seconds.
        SignalPeriodicity (1,1) {mustBePositive} = 2e-3

        %TransmitterPower Signal transmission power in dBm
        %   Specify the transmit power as a scalar double value. It
        %   specifies the signal transmission power in dBm. The default
        %   value is 20 dBm.
        TransmitterPower (1,1) {mustBeNumeric, mustBeFinite, ...
            mustBeLessThanOrEqual(TransmitterPower,100)} = 20
    end

    properties (SetAccess = private)
        %SimulationTime Current simulation time
        SimulationTime = 0;

        %TransmitBuffer Buffer contains the data to be transmitted from the
        %node
        TransmitBuffer

        %SampleRate Sample rate of the signal in Hz
        SampleRate

        %TransmittedSignals Total number of transmitted signals
        TransmittedSignals = 0

        %Waveform IQ samples of the generated waveform
        Waveform
    end

    properties (Access = private)
        %NextSignalTimer Timer to track the start of next signal
        pNextSignalTimer = 0

        %pTransmitBuffer Buffer contains the data to be transmitted from the
        %node
        pTransmitBuffer

        %pMetadata Structure containing the metadata of the signal
        pMetadata

        %pIsInitialized Flag to check whether the node is initialized
        pIsInitialized = false
    end

    events (Hidden)
        %PacketTransmissionStarted is triggered when the node starts
        %transmitting a packet. PacketTransmissionStarted passes the event
        %notification along with a structure to the registered callback. The
        %structure has fields in pTransmitBuffer property along woth PacketDuration
        %and TransmittedPower property
        PacketTransmissionStarted
    end

    methods
        % Constructor
        function obj = helperInterferingWLANNode(varargin)
            % Name-value pairs
            for idx = 1:2:nargin-1
                obj.(varargin{idx}) = varargin{idx+1};
            end

            % Initialize the WLAN signal
            obj.pMetadata = struct('NumSamples', 0, ...
                'Duration', 0);

            % Initialize the transmission buffer with empty signal
            obj.pTransmitBuffer = wirelessnetwork.internal.wirelessPacket;
        end

        % Auto-completion
        function v = set(obj, prop)
            v = obj.([prop, 'Values']);
        end

        % Set waveform source
        function set.WaveformSource(obj, value)
            obj.WaveformSource = validatestring(value, {'BasebandFile', ...
                'Generated'}, mfilename, 'WaveformSource');
        end

        function nextInvokeTime = run(obj, currentTime)
            %run Runs the WLAN node
            %
            %   NEXTINVOKETIME = run(OBJ, CURRENTTIME) runs the
            %   functionality of WLAN node at the current time,
            %   CURRENTTIME, and returns the time, NEXTINVOKETIME, to run
            %   the node again.
            %
            %   NEXTINVOKETIME is the time instant (in seconds) at which
            %   the node runs again.
            %
            %   OBJ is an object of type helperInterferingWLANNode.
            %
            %   CURRENTTIME is the current simulation time in seconds.

            % Initialize the node when the node is run for the first time
            if ~obj.pIsInitialized
                init(obj);
            end

            % Update the simulation time (in seconds)
            elapsedTime = currentTime - obj.SimulationTime;
            obj.SimulationTime = currentTime;

            % Update the timer
            obj.pNextSignalTimer = round(obj.pNextSignalTimer - elapsedTime, 9);

            % Reset the transmission buffer
            obj.TransmitBuffer = [];

            % Update the transmission buffer with the waveform and its
            % metadata
            if obj.pNextSignalTimer <= 0
                % Update the transmission buffer with the signal
                % information
                obj.TransmitBuffer = obj.pTransmitBuffer;
                obj.TransmitBuffer.StartTime = obj.SimulationTime;

                % Update the timer for next signal transmission
                obj.pNextSignalTimer = obj.SignalPeriodicity;

                % Update the number of signals transmitted
                obj.TransmittedSignals = obj.TransmittedSignals + 1;

                % Notify the packet transmission started event
                txBuffer = obj.TransmitBuffer;
                txBuffer.PacketDuration = obj.pTransmitBuffer.Duration;
                txBuffer.TransmittedPower = obj.pTransmitBuffer.Power;
                triggerEvent(obj,"PacketTransmissionStarted",txBuffer);
            end
            nextInvokeTime = round(obj.pNextSignalTimer + obj.SimulationTime, 9);
        end

        function txPackets = pullTransmittedData(obj)
            txPackets = obj.TransmitBuffer;
            obj.TransmitBuffer = [];
        end

        function pushReceivedData(~, ~)
            % Do nothing
        end

        function [flag, rxInfo] = isPacketRelevant(~, ~)
            %isPacketRelevant Return flag to indicate whether channel
            %has to be applied on incoming signal

            %The WLAN node is only for transmission and not for reception
            %hence the flag is set as false and rxInfo is set empty
            flag = false;
            rxInfo = [];
        end
    end

    methods (Access = private)
        function init(obj)
            %init Initializes the waveform and transmit buffer

            % Generate waveform
            switch obj.WaveformSource
                case 'BasebandFile'
                    % Extract the waveform from baseband file
                    bbReader = comm.BasebandFileReader('Filename', obj.BasebandFile);
                    bbInfo = info(bbReader);

                    % Configure the baseband file reader
                    bbReader.SamplesPerFrame = bbInfo.NumSamplesInData;

                    % Read the WLAN waveform from the baseband file
                    obj.Waveform = bbReader();
                    obj.SampleRate = bbReader.SampleRate;
   
                case 'Generated'
                    generateWaveform(obj);
            end

            % Apply Tx power on the waveform
            scale = 10.^((-30 + obj.TransmitterPower)/20);
            obj.Waveform = obj.Waveform * scale;

            % Update the transmission buffer with the signal information
            obj.pTransmitBuffer.Type = 1;
            obj.pTransmitBuffer.Metadata.NumSamples = numel(obj.Waveform);
            obj.pTransmitBuffer.Data = obj.Waveform;
            obj.pTransmitBuffer.SampleRate = obj.SampleRate;
            obj.pTransmitBuffer.TransmitterPosition = obj.Position;
            obj.pTransmitBuffer.TransmitterVelocity = obj.Velocity;
            obj.pTransmitBuffer.NumTransmitAntennas = 1;
            obj.pTransmitBuffer.Bandwidth = obj.Bandwidth;
            obj.pTransmitBuffer.CenterFrequency = obj.CenterFrequency;
            obj.pTransmitBuffer.TransmitterID = obj.ID;
            obj.pTransmitBuffer.Power = obj.TransmitterPower;
            obj.pTransmitBuffer.Duration = round(obj.pTransmitBuffer.Metadata.NumSamples/...
                obj.SampleRate, 9);

            if obj.pTransmitBuffer.Duration > obj.SignalPeriodicity
                error('Transmission duration of signal must be less than signal periodicity');
            end

            % Set initialization flag
            obj.pIsInitialized = true;
        end

        function generateWaveform(obj)
            % generateWaveform generates the WLAN waveform using the
            % features of WLAN Toolbox(TM)

            switch class(obj.FormatConfig)
                case {'wlanHESUConfig','wlanHETBConfig'}
                    dataLength = getPSDULength(obj.FormatConfig);
                case {'wlanHTConfig','wlanNonHTConfig'}
                    dataLength = obj.FormatConfig.PSDULength;
            end

            % Create random data
            data = randi([0 1], dataLength*8, 1);

            % Generate WLAN waveform
            obj.Waveform = wlanWaveformGenerator(data, obj.FormatConfig);
            obj.SampleRate = wlanSampleRate(obj.FormatConfig);
        end

        function triggerEvent(obj, eventName, eventData)
            %triggerEvent Trigger the event to notify all the listeners

            if event.hasListener(obj, eventName)
                eventData.NodeName = obj.Name;
                eventData.NodeID = obj.ID;
                eventData.CurrentTime = obj.SimulationTime;
                eventDataObj = wirelessnetwork.internal.nodeEventData;
                eventDataObj.Data = eventData;
                notify(obj, eventName, eventDataObj);
            end
        end
    end
end