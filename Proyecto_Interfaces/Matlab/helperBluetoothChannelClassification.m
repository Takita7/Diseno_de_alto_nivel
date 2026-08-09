classdef helperBluetoothChannelClassification < handle
    %helperBluetoothChannelClassification Create an object to classify
    %Bluetooth channels
    %   BLUETOOTHCLASSIFIER = helperBluetoothChannelClassification creates
    %   an object, BLUETOOTHCLASSIFIER, to classify Bluetooth channels
    %   using default values. This object classifies channels as "good"
    %   (used) or "bad" (unused) channels based on the packet error rate
    %   (PER) of each channel.
    %
    %   BLUETOOTHCLASSIFIER =
    %   helperBluetoothChannelClassification(Name=Value) creates a
    %   Bluetooth channel classification object, BLUETOOTHCLASSIFIER, with
    %   the specified property Name set to the specified Value. You can
    %   specify additional name-value pair arguments in any order as
    %   (Name1=Value1, ..., NameN=ValueN).
    %
    %   helperBluetoothChannelClassification properties:
    %
    %   PERThreshold                   - Packet error rate (PER) threshold
    %
    %   helperBluetoothChannelClassification properties (configurable through N-V pair only):
    %
    %   PreferredMinimumGoodChannels   - Minimum preferred number of good
    %                                    channels
    %   MinReceptionsToClassify        - Minimum number of received packets
    %                                    to classify the channels
    %
    %   helperBluetoothChannelClassification properties (read-only):
    %
    %   CentralNode     - Bluetooth Central node
    %   PeripheralNode  - Bluetooth Peripheral node
    %   ChannelMap      - Channel map of Bluetooth data channels
    %
    %   Algorithm: The PER is calculated from the last reception status for
    %   all the channels whose reception count is greater than the
    %   MinReceptionsToClassify. When the PER of the channel is less than
    %   the PERThreshold, the channel is classified as a bad channel. If
    %   the total number of good channels is less than
    %   PreferredMinimumGoodChannels then all the data channels are
    %   classified as good channels.

    %   Copyright 2021-2024 The MathWorks, Inc.

    properties
        %PERThreshold Packet error rate (PER) threshold
        %   Specify the packet error rate (PER) threshold as a double value
        %   in the range of [1, 100] percentage. This property specifies
        %   the threshold value of PER required to classify the channels.
        %   The default value is 40.
        PERThreshold (1, 1) {mustBeNumeric, mustBeInRange(PERThreshold, 1, 100)} = 40
    end

    properties (GetAccess = public,SetAccess = private)
        %CentralNode Bluetooth Central node
        %   This indicates the Central node associated with the Peripheral
        %   node whose physical link needs to be classified.
        CentralNode

        %PeripheralNode Bluetooth Peripheral node
        %   This indicates the Peripheral node whose channel used for the
        %   physical link needs to be classified at the Central node.
        PeripheralNode

        %PreferredMinimumGoodChannels Minimum preferred number of good
        %channels
        %   This property is an integer in the range [2,37] for Bluetooth
        %   LE and [20 79] for Bluetooth BR/EDR. This property specifies
        %   the preferred number of minimum good channels that need to be
        %   maintained for the data exchange between Bluetooth devices
        PreferredMinimumGoodChannels

        %MinReceivedCountToClassify Minimum number of received packets to
        %classify the channels
        %   Specify the minimum received count as an integer greater than
        %   4. This property specifies the minimum number of received
        %   packets and status to be used for classification of the
        %   channels. The default value is 4.
        MinReceptionsToClassify = 4

        %BufferSize Size of the reception buffer to store the reception
        %status
        BufferSize = 20

        %ChannelMap Channel map of Bluetooth data channels
        ChannelMap
    end

    properties (Access = private)
        %pLastRxStatus Status of the recent received packets
        %   pLastRxStatus is a two-dimensional array of size
        %   [obj.pNumDataChannels, obj.BufferSize].
        %   pLastRxStatus(channelNum,:) contains the status of previous
        %   receptions in the channel "channelNum". The received packet
        %   status value 0 represents "Failed", 1 represents "Success" and
        %   -1 indicates not yet received.
        pLastRxStatus

        %pRxIdx Current receiving packet index in pLastRxStatus array
        %   pRxIdx is a vector. pRxIdx(channelNum) represents the index in
        %   channel "channelNum".
        pRxIdx

        %pNumDataChannels Number of data channels based on the Bluetooth
        %standard
        pNumDataChannels

        %pIsBluetoothBREDR Flag to represent Bluetooth BR/EDR protocol
        %standard
        pIsBluetoothBREDR = false
    end

    methods
        function obj = helperBluetoothChannelClassification(centralNode,peripheralNode,varargin)
            %Constructor
            %
            %   CENTRALNODE is the Bluetooth Central node associated with
            %   the Peripheral node whose physical link needs to be
            %   classified, specified as an object of type bluetoothNode or
            %   bluetoothLENode with the Role property set as "Central".
            %
            %   PERIPHERALNODE is one of the connected nodes of the Central
            %   node whose physical link needs to be classified, specified
            %   as an object of type bluetoothNode or bluetoothLENode with
            %   the Role property set as "Peripheral".

            % Initialize based on the Bluetooth node
            if isa(peripheralNode, "bluetoothNode")
                obj.pIsBluetoothBREDR = true;
                obj.pNumDataChannels = 79;
                obj.PreferredMinimumGoodChannels = 20;
            else
                obj.pNumDataChannels = 37;
                obj.PreferredMinimumGoodChannels = 2;
            end
            % Set name-value pairs
            for idx = 1:2:nargin-2
                propertyName = varargin{idx};
                value = varargin{idx+1};
                if strcmpi(propertyName,'PreferredMinimumGoodChannels')
                    % Validate the preferred minimum good channels
                    validateattributes(value, {'double'}, ...
                        {'>=',obj.PreferredMinimumGoodChannels,'<',obj.pNumDataChannels,'scalar','integer'}, ...
                        mfilename,'PreferredMinimumGoodChannels',obj.PreferredMinimumGoodChannels);
                elseif strcmpi(propertyName,'MinReceptionsToClassify')
                    validateattributes(value,{'double'},{'>=',4,'scalar','integer'}, ...
                        mfilename, 'MinReceptionsToClassify', 20)
                    if value>obj.BufferSize
                        obj.BufferSize = value;
                    end
                end
                obj.(propertyName) = value;
            end
            % Update the channel map and Peripheral node
            usedChannels = centralNode.ConnectionConfig.UsedChannels;
            channelMapUsed = zeros(1,obj.pNumDataChannels);
            channelMapUsed(usedChannels+1) = 1;
            obj.ChannelMap = channelMapUsed;
            % Validate the Central node
            validateattributes(centralNode, ["bluetoothLENode","bluetoothNode"], {'scalar'});
            obj.CentralNode = centralNode;
            % Validate the Peripheral node
            validateattributes(peripheralNode, ["bluetoothLENode","bluetoothNode"], {'scalar'});
            obj.PeripheralNode = peripheralNode;
            verifyConnection(obj);

            % Initialize the status of the receptions to -1. These
            % receptions are per data channel. Value 1 indicates success, 0
            % indicates failure and -1 indicates not yet received.
            obj.pLastRxStatus = ones(obj.pNumDataChannels,obj.BufferSize)*-1;
            obj.pRxIdx = ones(1,obj.pNumDataChannels);

            % Use weak-references for cross-linking handle objects
            objWeakRef = matlab.lang.WeakReference(obj);

            % Add listener at the Central node  for the
            % PacketReceptionEnded event exposed by the bluetoothNode
            % object to receive packet reception information
            addlistener(obj.CentralNode,"PacketReceptionEnded", @(~,eventdata) objWeakRef.Handle.updateRxStatus(eventdata));
        end

        function updateRxStatus(obj,rxEventData)
            %updateRxStatus Updates the status of the received packet
            %
            %   updateRxStatus(OBJ,RXEVENTDATA) updates the status of the
            %   received packet and calculates the recent and overall
            %   success rates
            %
            %   OBJ is an object of type
            %   helperBluetoothChannelClassification.
            %
            %   RXEVENTDATA is a handle object with these properties:
            %       Data        : Structure with the fields containing
            %                     information about packet reception
            %       Source      : Source node object of type bluetoothNode
            %                     or bluetoothLENode
            %       EventName   : Name of the received event

            rxInfo = rxEventData.Data;
            % Get the node ID from where the Central received the
            % information
            if obj.pIsBluetoothBREDR
                sourceNodeID = rxInfo.SourceNodeID;
            else
                sourceNodeID = rxInfo.SourceID;
            end
            % Check if the Peripheral ID of the classifier object is same
            % as node ID of the received information
            if obj.PeripheralNode.ID == sourceNodeID
                indexPos = rxInfo.ChannelIndex+1;
                % Update the reception information
                obj.pLastRxStatus(indexPos,obj.pRxIdx(indexPos)) = rxInfo.SuccessStatus;
                obj.pRxIdx(indexPos) = obj.pRxIdx(indexPos)+1;
                % If the reception index of the received channel is greater
                % than the number of receptions reset the index
                if obj.pRxIdx(indexPos)>obj.BufferSize
                    obj.pRxIdx(indexPos) = 1;
                end
            end
        end

        function classifyChannels(obj)
            %classifyChannels Classifies the channels into good or bad
            %based on statistics of each channel
            %
            %   classifyChannels(OBJ) classifies the channels into good or
            %   bad based on statistics of each channel.
            %
            %   OBJ is an object of type
            %   helperBluetoothChannelClassification.

            % Classify each Bluetooth channel into either good or bad
            for idx = 1:obj.pNumDataChannels
                % Total number of received packets in the channel "idx"
                totalRxPackets = nnz(obj.pLastRxStatus(idx,:) ~= -1);

                % Ignore if the channel is already a bad channel or minimum
                % number of packets were not yet received in that channel
                if (obj.ChannelMap(idx) == false) || ...
                        (totalRxPackets<obj.MinReceptionsToClassify)
                    continue;
                end
                % Calculate PER
                per = (nnz(~obj.pLastRxStatus(idx,:))/totalRxPackets)*100;
                % Compare the calculated PER with threshold value to
                % classify the channel
                if (per>obj.PERThreshold)
                    obj.ChannelMap(idx) = false;
                end
            end
            % If the number of good channels is less than
            % "PreferredMinimumGoodChannels", classify all the bad channels
            % into good channels.
            if (nnz(obj.ChannelMap) < obj.PreferredMinimumGoodChannels)
                % Make all bad channels into good channels
                for idx = 1:obj.pNumDataChannels
                    % Ignore if the channel is already a good channel
                    if(obj.ChannelMap(idx) == true)
                        continue;
                    end
                    % Convert the bad channel a good channel
                    obj.ChannelMap(idx) = true;
                    % Reset all the statistics of the channel
                    obj.pLastRxStatus(idx,:) = -1;
                    obj.pRxIdx(idx) = 1;
                end
            end
            % Updates the channel list by providing a new list of good
            % channels for the specified destination
            channelList = find(obj.ChannelMap)-1;
            updateChannelList(obj.CentralNode,channelList,"DestinationNode",obj.PeripheralNode);
        end
    end

    methods (Access = private)
        function verifyConnection(obj)
            %verifyConnection Verifies the connectivity between Central and
            %Peripheral node

            validateattributes(obj.CentralNode,["bluetoothLENode","bluetoothNode"],{'scalar'});
            validateattributes(obj.PeripheralNode,["bluetoothLENode","bluetoothNode"],{'scalar'});
            if ~strcmp(class(obj.CentralNode),class(obj.PeripheralNode))
                error("The Central Node and Peripheral node must be of same class.");
            end
            if obj.pIsBluetoothBREDR
                [centralLTAddress{1:numel(obj.CentralNode.ConnectionConfig)}] = deal(obj.CentralNode.ConnectionConfig.PrimaryLTAddress);
                flag = find(cell2mat(centralLTAddress)==obj.PeripheralNode.ConnectionConfig.PrimaryLTAddress);
            else
                flag = false;
                [centralAccessAddress{1:numel(obj.CentralNode.ConnectionConfig)}] = deal(obj.CentralNode.ConnectionConfig.AccessAddress);
                peripheralAccessAddress = obj.PeripheralNode.ConnectionConfig.AccessAddress;
                for idx = 1:numel(obj.CentralNode.ConnectionConfig)
                    if strcmpi(centralAccessAddress{idx},peripheralAccessAddress)
                        flag = true;
                        break;
                    end
                end
            end
            if isempty(flag) || flag == false
                error("The Central node must be connected to the Peripheral node.");
            end
        end
    end
end