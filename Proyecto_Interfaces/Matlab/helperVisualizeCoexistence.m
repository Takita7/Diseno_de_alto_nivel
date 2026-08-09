classdef helperVisualizeCoexistence < handle
    %helperVisualizeCoexistence Create an object for Bluetooth and WLAN
    %coexistence visualization
    %   OBJ =
    %   helperVisualizeCoexistence(simulationTime,bluetoothNodes,wlanNodes)
    %   creates an object for Bluetooth and WLAN coexistence model
    %   visualization with default values.
    %
    %   OBJ = helperVisualizeCoexistence(simulationTime,bluetoothNodes)
    %   creates an object for Bluetooth visualization for no WLAN
    %   interference with default values.
    %
    %   helperVisualizeCoexistence properties (read-only):
    %
    %   SimulationTime      - Simulation stop-time
    %   BluetoothNodes      - Configured Bluetooth nodes in the network
    %   WLANNodes           - Configured WLAN nodes in the network
    %   SignalPeriodicity   - WLAN channel signal periodicity
    %   PeripheralCount     - Number of Bluetooth Peripherals
    %   OverallSuccessRates - Overall success rate per data channel of the
    %                         most recent classification
    %   RecentSuccessRates  - Recent success rate per data channel of the
    %                         most recent classification
    %   NumClassifications  - Number of channel classifications during the
    %                         simulation time
    %   PacketLossRatio     - List of packet loss ratios at the end of each
    %                         classification interval
    %   Throughput          - List of throughput at the end of each
    %                         classification interval

    %   Copyright 2018-2023 The MathWorks, Inc.

    properties (SetAccess = private)
        %SimulationTime Simulation stop-time
        %   Simulation time is a finite positive scalar indicating the
        %   simulation stop time in seconds.
        SimulationTime

        %BluetoothNodes Configured Bluetooth nodes in the network
        %   BluetoothNodes is an array of all the Bluetooth nodes in the
        %   network.
        BluetoothNodes = []

        %WLANNodes Configured WLAN Nodes in the network
        %   WLANNodes is an array of all the WLAN nodes in the network.
        WLANNodes = []

        %PeripheralCount Number of Bluetooth Peripherals
        %   Peripheral count is an integer value representing the total
        %   number of Bluetooth Peripherals. The default value is 0.
        PeripheralCount = 0

        %OverallSuccessRates Overall success rate per data channel of the
        %most recent classification
        %   OverallSuccessRates is a cell array of overall success rates
        %   per data channel from the start of simulation till the current
        %   time
        OverallSuccessRates

        %RecentSuccessRates Success rate per data channel of the most
        %recent classification
        %   RecentSuccessRates is a cell array of recent success rates per
        %   data channel after the previous classification till the most
        %   recent classification
        RecentSuccessRates

        %NumClassifications Number of channel classifications during the
        %simulation time
        NumClassifications

        %PacketLossRatio List of packet loss ratios at the end of each
        %classification interval of the Peripheral
        PacketLossRatio

        %Throughput List of throughput at the end of each classification
        %interval of the Peripheral
        Throughput
    end

    properties (Hidden, Constant)
        % PHY mode values of Bluetooth LE standard
        PHYModeValuesBluetoothLE = ["LE2M", "LE1M", "LE500K", "LE125K"];

        % PHY mode values of Bluetooth BR/EDR standard
        PHYModeValuesBREDR = ["BR", "EDR2M", "EDR3M"];

        % Start point of the figure
        PlotStartCoordinateX = 2;

        % Bluetooth channel dimensions
        ChannelLength = 3.2;

        % Progress bar dimensions
        ProgressBarDim = [0.85 0.025 0.08 0.02];

        % Bluetooth and WLAN channel colors in Hexadecimal color code
        AdvertisingChannelColor = "--mw-backgroundColor-hover";
        GoodChannelColor = "--mw-graphics-colorOrder-5-tertiary";
        BadChannelColor = "--mw-graphics-colorOrder-10-secondary";
        RxSuccessChannelColor = "--mw-backgroundColor-success";
        RxFailureChannelColor = "--mw-backgroundColor-error";
        WLANChannelColor = "--mw-graphics-colorOrder-6-secondary";
    end

    properties (Access = private)
        %pPeripheralNodes List of configured Peripheral nodes in the
        %network
        pPeripheralNodes

        %WLANChannelCoordinateY WLAN channel y- coordinate
        pWLANChannelCoordinateY = 7.5

        %pChannelFig Cell array indicating the figure handles of each
        %Bluetooth channel in the coexistence model. The size of the cell
        %array is [Number of Peripherals, Number of data channels]
        pChannelFig

        %pModelFig Cell array indicating the figure handles of each
        %nodes coexistence model. The size of the cell array is the number
        %of Peripherals.
        pModelFig

        %pOverallSuccessRateFig Cell array indicating the figure handles of
        %each nodes overall success rate model. The size of the cell array
        %is the number of Peripherals.
        pOverallSuccessRateFig

        %pRecentSuccessRateFig Cell array indicating the figure handles of
        %each nodes recent success rate model. The size of the cell array
        %is the number of Peripherals.
        pRecentSuccessRateFig

        %pCoexistenceFigs Cell array indicating the figure handles of each
        %nodes complete coexistence model. The size of the cell array is
        %the number of Peripherals.
        pCoexistenceFigs

        %pProgressFig Cell array indicating the figure handles of each
        %nodes progress figure. The size of the cell array is the number of
        %Peripherals.
        pProgressFig

        %pPercentageFig Cell array indicating the figure handles of
        %each nodes progress box. The size of the cell array is the number
        %of Peripherals.
        pPercentageFig

        %pIsInitialized Flag to check whether the visualization is
        %initialized or not
        pIsInitialized = false

        %pIsBluetoothBREDR Array of flags representing if the each of the
        %Peripheral node uses the Bluetooth BR/EDR protocol standard. The
        %size of the array is the number of Peripherals.
        pIsBluetoothBREDR = false

        %pRxCount Cell array indicating the number of packets received in
        %each data channel at the beginning of each classification
        %interval. The size of the cell array is the number of Peripherals.
        pRxCount

        %pRxFailureCount Cell array indicating the number of packets failed
        %in each data channel at the beginning of each classification. The
        %size of the cell array is the number of Peripherals.
        pRxFailureCount

        %pChannelMaps Array of channel maps at the beginning of each
        %classification interval. The size of the array is the number of
        %channel classifications
        pChannelMaps
    end

    methods
        %Constructor
        function obj = helperVisualizeCoexistence(simulationTime,bluetoothNodes,varargin)
            obj.SimulationTime = simulationTime;
            if iscell(bluetoothNodes)
                for idx = 1:numel(bluetoothNodes)
                    validateattributes(bluetoothNodes{idx}, ["bluetoothLENode","bluetoothNode"], {'scalar'});
                end
                obj.BluetoothNodes = bluetoothNodes;
            else
                validateattributes(bluetoothNodes(1), ["bluetoothLENode","bluetoothNode"], {'scalar'});
                obj.BluetoothNodes = num2cell(bluetoothNodes);
            end
            narginchk(2,3);
            if nargin==3
                wlanNodes = varargin{1};
                if iscell(wlanNodes)
                    for idx = 1:numel(wlanNodes)
                        validateattributes(wlanNodes{idx}, "helperInterferingWLANNode", {'scalar'});
                    end
                    obj.WLANNodes = wlanNodes;
                else
                    validateattributes(wlanNodes(1), "helperInterferingWLANNode", {'scalar'});
                    obj.WLANNodes = num2cell(wlanNodes);
                end
            end
            peripheralIndex = 1;
            for idx = 1:numel(bluetoothNodes)
                if strcmpi(obj.BluetoothNodes{idx}.Role, "Central")
                    % Add listeners to the Central node for the
                    % ChannelMapUpdated and PacketReceptionEnded event
                    % exposed by the bluetoothNode object to update the
                    % channel map and status of every reception
                    addlistener(obj.BluetoothNodes{idx},"ChannelMapUpdated", ...
                        @(nodeobj,eventdata) updateBluetoothVisualization(obj,nodeobj,eventdata));
                    addlistener(obj.BluetoothNodes{idx},"PacketReceptionEnded", ...
                        @(nodeobj,eventdata) updateBluetoothVisualization(obj,nodeobj,eventdata));
                else
                    % Assign the Peripheral nodes
                    obj.pPeripheralNodes{peripheralIndex} = obj.BluetoothNodes{idx};
                    peripheralIndex = peripheralIndex+1;
                end
            end
            obj.PeripheralCount = peripheralIndex-1;

            networkSimulator = wirelessNetworkSimulator.getInstance();

            % Schedule action at the end of simulation to update the
            % progress bar
            scheduleAction(networkSimulator,@(varargin) UpdateProgressBarAtEnd(obj),[],simulationTime);
        end
    end

    methods
        function initializeVisualization(obj)
            %initializeVisualization Initialize the Bluetooth WLAN
            %coexistence visualization
            %
            %   OBJ is an object of type helperVisualizeCoexistence

            import matlab.graphics.internal.themes.specifyThemePropertyMappings
            peripheralsCount = obj.PeripheralCount;
            phyMode = "";
            numChannels = ones(1,peripheralsCount);
            for peripheralIdx = 1:peripheralsCount
                if isa(obj.pPeripheralNodes{peripheralIdx}, "bluetoothNode")
                    obj.pIsBluetoothBREDR(peripheralIdx) = true;
                    numDataChannels = 79;
                    numChannels(peripheralIdx) = numDataChannels;
                    phyMode(peripheralIdx) = "BR/EDR";
                    preallocationValue = ceil(obj.SimulationTime*1e6/(625*obj.pPeripheralNodes{peripheralIdx}.ConnectionConfig.InstantOffset));
                else
                    obj.pIsBluetoothBREDR(peripheralIdx) = false;
                    numDataChannels = 37;
                    numChannels(peripheralIdx) = numDataChannels+3;
                    phyMode(peripheralIdx) = obj.pPeripheralNodes{peripheralIdx}.ConnectionConfig.PHYMode;
                    preallocationValue = ceil(obj.SimulationTime/obj.pPeripheralNodes{peripheralIdx}.ConnectionConfig.ConnectionInterval);
                end
                obj.OverallSuccessRates{peripheralIdx} = zeros(1,numDataChannels);
                obj.RecentSuccessRates{peripheralIdx} = zeros(1,numDataChannels);
                obj.PacketLossRatio(peripheralIdx,1:preallocationValue) = 0;
                obj.Throughput(peripheralIdx,1:preallocationValue) = 0;
                obj.pRxCount{peripheralIdx} = zeros(preallocationValue,numDataChannels);
                obj.pRxFailureCount{peripheralIdx} = zeros(preallocationValue,numDataChannels);
                obj.pChannelMaps{peripheralIdx} = ones(preallocationValue,numDataChannels);
                phyMode(peripheralIdx) = strjoin([" (" phyMode(peripheralIdx) " PHY)"],"");
            end
            obj.NumClassifications(1:peripheralsCount) = 0;
            obj.pChannelFig = cell(peripheralsCount,numDataChannels);
            obj.pCoexistenceFigs = cell(1, peripheralsCount);
            [obj.pModelFig, successRateFig, obj.pOverallSuccessRateFig,obj.pRecentSuccessRateFig,...
                obj.pProgressFig,obj.pPercentageFig] = deal(cell(peripheralsCount, 1));

            resolution = get(0,"screensize"); % Get screen resolution
            for peripheralIdx = 1:peripheralsCount
                % Initialize a figure for each peripheral
                obj.pCoexistenceFigs{peripheralIdx} = figure("Position" , ...
                    [resolution(3)*0.1,resolution(4)*(0.05),resolution(3)*0.8,resolution(4)*0.7], ...
                    "MenuBar","none");
                matlab.graphics.internal.themes.figureUseDesktopTheme(obj.pCoexistenceFigs{peripheralIdx});
                coexistenceFigAxes = axes(obj.pCoexistenceFigs{peripheralIdx},"Visible","off");
                coexistenceFigAxes(2) = copyobj(coexistenceFigAxes(1), obj.pCoexistenceFigs{peripheralIdx});

                % Create a sub-plot for coexistence model visualization
                coexistenceFigAxes(1).Visible = "Off";
                obj.pModelFig{peripheralIdx} = subplot(2,1,1,coexistenceFigAxes(1));
                obj.pModelFig{peripheralIdx}.Position = [0.12 0.5 0.78 0.5];
                obj.pModelFig{peripheralIdx}.FontUnits = "normalized";
                hold(coexistenceFigAxes(1),"on");

                dimensionText = [0.035 0.674 0.3 0.3];
                dimensionBox = [0.014 0.9525 0.020 0.014];
                annotationOffsets = [0.0012 0.027 0.022 0.028 0.025 0.026 0.026];
                annotationString = ["Advertising channel";"WLAN channel with";"signal periodicity";"Good channel"; ...
                    "Bad channel";"Reception success";"Reception failure"];
                % Annotation strings
                if ~obj.pIsBluetoothBREDR(peripheralIdx)
                    addAnnotationText(obj,annotationString(1),dimensionText,peripheralIdx);
                    addAnnotationBox(obj,dimensionBox,obj.AdvertisingChannelColor,peripheralIdx);
                end
                for idx = 2:7
                    dimensionText(2) = dimensionText(2)-annotationOffsets(idx);
                    addAnnotationText(obj,annotationString(idx),dimensionText,peripheralIdx);
                end
                % Channel annotations
                channelOffsets = [0.027 0.05 0.025 0.025 0.025];
                channelColors = [obj.WLANChannelColor;obj.GoodChannelColor;obj.BadChannelColor;...
                    obj.RxSuccessChannelColor;obj.RxFailureChannelColor];
                for idx = 1:5
                    dimensionBox(2) = dimensionBox(2)-channelOffsets(idx);
                    addAnnotationBox(obj,dimensionBox,channelColors(idx),peripheralIdx);
                end
                % Set axis limits
                axis(obj.pModelFig{peripheralIdx}, [0 260/(1+~obj.pIsBluetoothBREDR(peripheralIdx)) 4 12]);
                axis(obj.pModelFig{peripheralIdx}, "off")
                lineX= [obj.PlotStartCoordinateX obj.PlotStartCoordinateX]-obj.pIsBluetoothBREDR(peripheralIdx);
                lineY= [7 11];

                % Title for the model
                text(obj.pModelFig{peripheralIdx},40,11.5,['2.4 GHz Bluetooth and WLAN Coexistence Model' ...
                    char(phyMode(peripheralIdx))],"FontWeight","bold","FontSize",14,"FontUnits","normalized");

                % Add Bluetooth channels
                nextChannelCoordinateX = 1+~obj.pIsBluetoothBREDR(peripheralIdx);
                bluetoothChannelText(1:numChannels(peripheralIdx)) = text(obj.pModelFig{peripheralIdx});
                for idx = 1:numChannels(peripheralIdx)
                    obj.pChannelFig{peripheralIdx,idx} = rectangle(obj.pModelFig{peripheralIdx});
                    if ~obj.pIsBluetoothBREDR(peripheralIdx)
                        nextChannelCoordinateX = nextChannelCoordinateX+obj.ChannelLength;
                    end
                    bleChannelCoordinateX = nextChannelCoordinateX;

                    % Re-order the Bluetooth advertisement channels
                    advertisementOffset = [0 12 39];
                    if obj.pIsBluetoothBREDR(peripheralIdx)
                        textFontSize = 7;
                        nextChannelCoordinateX = nextChannelCoordinateX+obj.ChannelLength;
                        color = obj.GoodChannelColor;
                    else
                        textFontSize = 8;
                        if any(idx == [38 39 40])
                            bleChannelCoordinateX = obj.PlotStartCoordinateX+(advertisementOffset(idx-37)*obj.ChannelLength);
                            color = obj.AdvertisingChannelColor;
                        else
                            color = obj.GoodChannelColor;
                            if(idx > 11)
                                bleChannelCoordinateX = bleChannelCoordinateX+obj.ChannelLength;
                            end
                        end
                    end
                    obj.pChannelFig{peripheralIdx,idx}.Position = [bleChannelCoordinateX 2 obj.ChannelLength 5];
                    specifyThemePropertyMappings(obj.pChannelFig{peripheralIdx,idx},'FaceColor',color);
                    obj.pChannelFig{peripheralIdx,idx}.Curvature = [1, 0.6];
                    plotHandle = plot(obj.pModelFig{peripheralIdx},lineX,lineY,":");
                    specifyThemePropertyMappings(plotHandle,'color',"--mw-graphics-borderColor-axes-secondary");
                    lineX = lineX+obj.ChannelLength;

                    % Add channel number text to each Bluetooth channel
                    bluetoothChannelText(idx) = text(obj.pModelFig{peripheralIdx}, bleChannelCoordinateX+1.5, ...
                        5.2, ['Channel-' num2str(idx-1)], "Rotation", 90, "FontSize", textFontSize,...
                        "HorizontalAlignment", "center","FontUnits", "normalized");
                    specifyThemePropertyMappings(plotHandle,'color',"--mw-color-primary");

                    % Show the center frequency of each Bluetooth channel
                    if obj.pIsBluetoothBREDR(peripheralIdx)
                        centerFreq = num2str(2402+(idx-1));
                    else
                        centerFreq = num2str(2400+idx*2);
                    end
                    text(obj.pModelFig{peripheralIdx}, nextChannelCoordinateX-1.5, 3.2, ...
                        [centerFreq, ' MHz'], "FontSize", textFontSize, "Rotation", 90, ...
                        "HorizontalAlignment", "center", "FontUnits", "normalized");
                end

                text(obj.pModelFig{peripheralIdx},-5,4,{obj.pPeripheralNodes{peripheralIdx}.Name, ...
                    "Channel map"}, "FontWeight", "bold", "FontSize", 12, "FontUnits", "normalized", ...
                    "Rotation", 90);
                plotHandle = plot(obj.pModelFig{peripheralIdx},lineX,lineY,":");
                specifyThemePropertyMappings(plotHandle,'color',"--mw-graphics-borderColor-axes-secondary");

                % Create a sub-plot for overall success rate per channel
                coexistenceFigAxes(2).Visible = "On";
                coexistenceFigAxes(2).FontSize = 8;
                successRateFig{peripheralIdx} = subplot(2, 1, 2, coexistenceFigAxes(2));
                successRateFig{peripheralIdx}.Position = [0.125+0.01*~obj.pIsBluetoothBREDR(peripheralIdx) 0.1 ...
                    0.76+0.005*~obj.pIsBluetoothBREDR(peripheralIdx) 0.28];
                successRateFig{peripheralIdx}.FontUnits = "normalized";
                axis(successRateFig{peripheralIdx}, [0.5, numChannels(peripheralIdx)+0.5, 1, 119])
                % Add labels to X and Y axes
                xticks(coexistenceFigAxes(2), 0:numChannels(peripheralIdx))
                if obj.pIsBluetoothBREDR(peripheralIdx)
                    xlabel(successRateFig{peripheralIdx}, 'Bluetooth BR Channel Number', ...
                        "FontUnits", "normalized", "FontWeight", "bold");
                    xticklabels(coexistenceFigAxes(2), [" " string(0:78)]);
                else
                    xlabel(successRateFig{peripheralIdx}, 'Bluetooth LE Channel Number', ...
                        "FontUnits", "normalized", "FontWeight", "bold");
                    xticklabels(coexistenceFigAxes(2), [" " "37" string(0:10) "38" string(11:36) "39"]);
                end
                ylabel(successRateFig{peripheralIdx}, "Success Rate (%)", ...
                    "FontUnits", "normalized", "FontWeight", "bold","FontSize",0.05);

                hold(coexistenceFigAxes(2),"on");
                % Add title to the sub-plot
                title(successRateFig{peripheralIdx}, "Success rate per channel", ...
                    "Units", "normalized", "Position", [0.5, 0.9, 0], 'FontSize', 12, ...
                    "FontUnits", "normalized");
                obj.pRecentSuccessRateFig{peripheralIdx} = bar(successRateFig{peripheralIdx}, ...
                    (0:numChannels(peripheralIdx)-1)+1.1, zeros(1, numChannels(peripheralIdx)),"BarWidth", 0.3);
                specifyThemePropertyMappings(obj.pRecentSuccessRateFig{peripheralIdx},'FaceColor',"--mw-graphics-colorOrder-6-secondary");

                obj.pOverallSuccessRateFig{peripheralIdx} = bar(successRateFig{peripheralIdx}, ...
                    (0:numChannels(peripheralIdx)-1)+0.8, zeros(1, numChannels(peripheralIdx)),"BarWidth", 0.3);
                specifyThemePropertyMappings(obj.pOverallSuccessRateFig{peripheralIdx},'FaceColor',"--mw-graphics-colorOrder-6-quaternary");

                legend(successRateFig{peripheralIdx}, "Recent success rate", ...
                    "Cumulative success rate","Location","None","Position", [0.707 0.348 0.08 0.056],"FontSize",8,"FontWeight","bold")
                % Add progress bar and display text
                annotation(obj.pCoexistenceFigs{peripheralIdx}, "rectangle",obj.ProgressBarDim);
                % specifyThemePropertyMappings(annotationHandle,'FaceColor',"--mw-color-tertiary");
                obj.pProgressFig{peripheralIdx} = annotation(obj.pCoexistenceFigs{peripheralIdx},"rectangle",obj.ProgressBarDim);
                specifyThemePropertyMappings(obj.pProgressFig{peripheralIdx},'FaceColor',"--mw-graphics-colorOrder-5-secondary");
                obj.pProgressFig{peripheralIdx}.Position(3) = 0;
                obj.pPercentageFig{peripheralIdx} = annotation(obj.pCoexistenceFigs{peripheralIdx}, "textbox", ...
                    obj.ProgressBarDim, "String", "0%","FitBoxToText", "on", "FontUnits", "normalized", ...
                    "LineStyle", "none", "HorizontalAlignment", "center","VerticalAlignment", "middle");
                specifyThemePropertyMappings(obj.pProgressFig{peripheralIdx},'Color',"--mw-color-primary");
                obj.pWLANChannelCoordinateY(peripheralIdx) = 7.5;
            end
            addWLANChannel(obj);
            for peripheralIdx = 1:peripheralsCount
                if obj.pIsBluetoothBREDR(peripheralIdx)
                    channelMap = zeros(1,79);
                else
                    channelMap = zeros(1,37);
                end
                channelMap(obj.pPeripheralNodes{peripheralIdx}.ConnectionConfig.UsedChannels+1) = 1;
                obj.pChannelMaps{peripheralIdx}(1,:) = channelMap;
                updateChannelMap(obj,channelMap,peripheralIdx);
            end
        end

        function updateBluetoothVisualization(obj,node,rxEventData)
            %updateBluetoothVisualization Updates the visualization based
            %on the events
            %   updateBluetoothVisualization(OBJ,NODE,RXEVENTDATA) updates
            %   the visualization
            %
            %   OBJ is an object of type helperVisualizeCoexistence
            %
            %   NODE is an object of type bluetoothLENode or bluetoothNode
            %
            %   RXEVENTDATA is a structure with these fields:
            %       Data        : Structure with the fields containing
            %                     information about packet reception
            %       Source      : Source node object of type bluetoothNode
            %                     or bluetoothLENode
            %       EventName   : Name of the received event

            if ~obj.pIsInitialized
                obj.pIsInitialized = true;
                initializeVisualization(obj);
            end

            eventName = rxEventData.EventName;
            eventDataInfo = rxEventData.Data;
            if strcmpi(eventName,"PacketReceptionEnded")
                if isfield(eventDataInfo,'SourceNodeID')
                    sourceNodeID = eventDataInfo.SourceNodeID;
                else
                    sourceNodeID = eventDataInfo.SourceID;
                end
                % Get the node ID from where the Central received the
                % information
                for idx = 1:obj.PeripheralCount
                    if  obj.pPeripheralNodes{idx}.ID == sourceNodeID
                        peripheralIdx = idx;
                        break;
                    end
                end
                channelNum = eventDataInfo.ChannelIndex+1;
                calculateSuccessRate(obj,channelNum,eventDataInfo.SuccessStatus,peripheralIdx);
                channelColor = obj.RxFailureChannelColor;
                if eventDataInfo.SuccessStatus
                    channelColor = obj.RxSuccessChannelColor;
                end
                if isvalid(obj.pCoexistenceFigs{peripheralIdx})
                    displayTransmissionStatus(obj,eventDataInfo.CurrentTime,channelNum,channelColor,peripheralIdx);
                end
                % Update the channel map in the visualization
            else
                if isfield(eventDataInfo,'PeerNodeID')
                    channelMap = zeros(1,79);
                    peerNodeID = eventDataInfo.PeerNodeID;
                else
                    channelMap = zeros(1,37);
                    peerNodeID = eventDataInfo.PeerID;
                end
                % Find the peripheral number and the corresponding
                % classifier the event is notified
                for idx = 1:obj.PeripheralCount
                    if  obj.pPeripheralNodes{idx}.ID == peerNodeID
                        peripheralIdx = idx;
                        break;
                    end
                end
                if isvalid(obj.pCoexistenceFigs{peripheralIdx})
                    channelMap(eventDataInfo.UpdatedChannelList+1) = 1;
                    updateChannelMap(obj,channelMap,peripheralIdx);
                    performanceStatistics(obj,node,obj.pPeripheralNodes{peripheralIdx},peripheralIdx);
                    % Update the number of channel classifications
                    obj.NumClassifications(peripheralIdx) = obj.NumClassifications(peripheralIdx)+1;
                    % Reset the reception counters
                    resetReceptionCounters(obj,channelMap,peripheralIdx);
                end
            end
        end

        function classificationStats= classificationStatistics(obj,centralNode,peripheralNode)
            %classificationStatistics Returns the Bluetooth channel
            %classification statistics
            %
            %   BLUETOOTHCLASSIFICATIONSTATISTICS =
            %   classificationStatistics(OBJ,CENTRALNODE,PERIPHERALNODE)
            %   displays the statistics in the form of a table. This
            %   function also visualizes the packet loss ratio and
            %   throughput between each channel map update for the
            %   Peripheral node.
            %
            %   OBJ is an object of type helperVisualizeCoexistence.
            %
            %   CENTRALNODE and PERIPHERALNODE is the Bluetooth Central and
            %   Peripheral node associated with each other. This is
            %   specified as an object of type bluetoothNode or
            %   bluetoothLENode with the Role property set as "Central" and
            %   "Peripheral", respectively.
            %
            %   CLASSIFICATIONSTATS is a struct containing the
            %   classification statistics with the each column index
            %   representing the channel index incremented by 1.

            for idx = 1:obj.PeripheralCount
                if  obj.pPeripheralNodes{idx}.ID == peripheralNode.ID
                    peripheralIndex = idx;
                    break;
                end
            end
            verifyConnection(obj,centralNode,peripheralNode,peripheralIndex);

            % Number of columns for each table for Peripheral statistics
            ncols = size(obj.pRxCount{peripheralIndex},2);
            % Column names
            variableNames = "";

            % Column names
            for idx = 1:ncols
                variableNames(idx) = strjoin(["Channel" idx-1]);
            end

            % Create an empty table with row names
            count = 1;
            numClassifications = obj.NumClassifications(peripheralIndex);
            rowNames = "";
            for classificationIdx = 1:numClassifications
                rowNames(count) = strjoin(["ChannelStatusTillClassification_" classificationIdx],"");
                rowNames(count+1) = strjoin(["RxPacketsTillClassification_" classificationIdx],"");
                rowNames(count+2) = strjoin(["RxPacketsFailedTillClassification_" classificationIdx],"");
                count = count+3;
            end
            rowNames(count) = "ChannelStatusTillSimulationEnds";
            rowNames(count+1) = "RxPacketsTillSimulationEnds";
            rowNames(count+2) = "RxPacketsFailedTillSimulationEnds";
            nrows = numel(rowNames);

            % Create an array of all the statistics in the order of
            % rowNames
            classificationStats = struct;
            classificationStatsArray = zeros(nrows,ncols);
            count = 1;
            for idx=1:numClassifications+1
                classificationStatsArray(count:count+2,:) = [obj.pChannelMaps{peripheralIndex}(idx,:);...
                    obj.pRxCount{peripheralIndex}(idx,:);obj.pRxFailureCount{peripheralIndex}(idx,:)];
                count = count+3;
            end
            for idx = 1:numel(rowNames)
                classificationStats.(rowNames(idx))  = classificationStatsArray(idx,:);
            end
            classificationStatsTable = array2table(classificationStatsArray,"RowNames",rowNames,"VariableNames",variableNames);

            % Display the channel classification statistics table
            fprintf("Channel classification statistics of %s \n", peripheralNode.Name);
            numericFormatOfDisplay = format;
            format short;
            disp(classificationStatsTable);
            format(numericFormatOfDisplay.NumericFormat);

            % Calculate the performance statistics after last
            % classification
            performanceStatistics(obj,centralNode,peripheralNode,peripheralIndex);
            % Display the performance graph when there is valid
            % transmissions
            if any(obj.Throughput(peripheralIndex))
                % Creates the packet loss ratio and throughput bar graph
                displayPerformanceGraph(obj,peripheralNode,peripheralIndex,numClassifications);
            end
        end
    end

    methods (Access = private)
        function addAnnotationText(obj,annotationText,dimensionText,modelIdx)
            %addAnnotationText Adds the annotation text at the specified
            %position and color

            annotation(obj.pCoexistenceFigs{modelIdx}, 'textbox', ...
                dimensionText, 'String', annotationText, ...
                'FitBoxToText', 'on', 'FontUnits', 'normalized', ...
                'LineStyle', 'none', 'Units', 'normalized');
        end

        function addAnnotationBox(obj,dimensionBox,boxColor,modelIdx)
            %addAnnotationBox Adds the annotation as a rectangle at the
            %specified position and color in the coexistence model

            import matlab.graphics.internal.themes.specifyThemePropertyMappings
            a = annotation(obj.pCoexistenceFigs{modelIdx},'rectangle',dimensionBox,...
                'Units','normalized');
            specifyThemePropertyMappings(a,'FaceColor',boxColor);
        end
        function displayTransmissionStatus(obj,currentTime,channelNumber,channelColor,peripheralIndex)
            %displayTransmissionStatus Update the visualization model with
            %a successful transmission

            import matlab.graphics.internal.themes.specifyThemePropertyMappings
            % Update success rate
            updateSuccessRate(obj,peripheralIndex);
            % Calculate the simulation progress
            percentage = (currentTime/(obj.SimulationTime))*100;
            obj.pProgressFig{peripheralIndex}.Position(3) = obj.ProgressBarDim(3)*(percentage/100);
            obj.pPercentageFig{peripheralIndex}.String = [ num2str(round(percentage)) '%'];

            % Highlight the current Bluetooth channel with a blink
            specifyThemePropertyMappings(obj.pChannelFig{peripheralIndex,channelNumber}, ...
                "FaceColor",channelColor);
            pause(0.05) % To visualize the active Bluetooth channel
            specifyThemePropertyMappings(obj.pChannelFig{peripheralIndex,channelNumber}, ...
                "FaceColor",obj.GoodChannelColor);
        end

        function updateChannelMap(obj,channelMap,peripheralIndex)
            %updateChannelMap Update the channel map in the visualization
            %model

            import matlab.graphics.internal.themes.specifyThemePropertyMappings
            for idx = 1:numel(channelMap)
                if channelMap(idx) == 1
                    specifyThemePropertyMappings(obj.pChannelFig{peripheralIndex,idx}, ...
                        "FaceColor",obj.GoodChannelColor);
                else
                    specifyThemePropertyMappings(obj.pChannelFig{peripheralIndex,idx}, ...
                        "FaceColor",obj.BadChannelColor);
                end
            end
        end

        function resetReceptionCounters(obj,channelMap,peripheralIndex)
            %resetReceptionCounters Resets the reception counters for every
            %channel map update

            numClassifications = obj.NumClassifications(peripheralIndex);
            obj.pRxCount{peripheralIndex}(numClassifications+1,:) = ...
                obj.pRxCount{peripheralIndex}(numClassifications,:);
            obj.pRxFailureCount{peripheralIndex}(numClassifications+1,:) = ...
                obj.pRxFailureCount{peripheralIndex}(numClassifications,:);
            obj.pChannelMaps{peripheralIndex}(numClassifications+1,:) = channelMap;
        end

        function addWLANChannel(obj)
            %addWLANChannel Adds a WLAN Channel to the visualization model

            import matlab.graphics.internal.themes.specifyThemePropertyMappings
            wlanCenterFrequencies = [2412:5:2472 2484]*1e6;
            mappedWLANChannels = containers.Map(wlanCenterFrequencies,1:14);
            for wlanIndex = 1:numel(obj.WLANNodes)
                signalPeriodicity = obj.WLANNodes{wlanIndex}.SignalPeriodicity;
                wlanBW = obj.WLANNodes{wlanIndex}.Bandwidth;

                for peripheralIdx = 1:obj.PeripheralCount
                    wlanChannelHeight = 0.3;
                    % Additional bandwidth space required in case of a
                    % larger WLAN bandwidth
                    if wlanBW == 40e6
                        additionBandwidthDisplay = 10;
                    elseif wlanBW == 80e6
                        additionBandwidthDisplay = 30;
                    else
                        additionBandwidthDisplay = 0;
                    end
                    wlanChannelNumber = mappedWLANChannels(obj.WLANNodes{wlanIndex}.CenterFrequency);

                    % Add a WLAN device to the figure
                    if obj.pIsBluetoothBREDR(peripheralIdx)
                        channelLengthDivFactor = 1;
                        wlanFactor = 20;
                    else
                        wlanFactor = 10;
                        channelLengthDivFactor = 2;
                    end
                    isChannel14 = wlanChannelNumber==14;
                    wlanChannelWidth = wlanFactor*obj.ChannelLength*((wlanBW*1e-6/20)*~isChannel14)+...
                        wlanFactor*obj.ChannelLength*isChannel14;
                    leftPos = (obj.PlotStartCoordinateX/(obj.pIsBluetoothBREDR(peripheralIdx)+1))+(obj.ChannelLength/2)+...
                        ((wlanChannelNumber-1)*(obj.ChannelLength/channelLengthDivFactor)*5)- ...
                        (additionBandwidthDisplay*(obj.ChannelLength/channelLengthDivFactor))+ ...
                        (isChannel14*7*(obj.ChannelLength/channelLengthDivFactor));
                    wlanChannel = rectangle(obj.pModelFig{peripheralIdx}, "Position", ...
                        [leftPos obj.pWLANChannelCoordinateY(peripheralIdx) wlanChannelWidth ...
                        wlanChannelHeight], "Clipping", "off");
                    specifyThemePropertyMappings(wlanChannel,'FaceColor',obj.WLANChannelColor);
                    wlanChanPos = wlanChannel.Position;
                    % Additional space required for a larger WLAN bandwidth
                    additionalSpace = (leftPos+wlanChannelWidth)/2;
                    if wlanBW ~= 20e6
                        if wlanChanPos(1)+6+additionalSpace < 20
                            wlanTextPos = wlanChanPos(1)+6+additionalSpace;
                        elseif wlanChanPos(1)+6+additionalSpace > 253
                            wlanTextPos = wlanChanPos(1)+6;
                        else
                            wlanTextPos = wlanChanPos(1)+30;
                        end
                    else
                        wlanTextPos = wlanChanPos(1)+6;
                    end
                    % Add channel number and signal periodicity text
                    text(obj.pModelFig{peripheralIdx}, wlanTextPos, wlanChanPos(2)+0.18, ...
                        ['WLAN channel ' num2str(wlanChannelNumber) ' (' ...
                        num2str(signalPeriodicity*1e3) ' ms)'],"FontUnits", "normalized","FontWeight","bold");
                    obj.pWLANChannelCoordinateY(peripheralIdx) = ...
                        obj.pWLANChannelCoordinateY(peripheralIdx)+wlanChannelHeight+0.1;
                end
            end
        end

        function updateSuccessRate(obj,peripheralIndex)
            %updateSuccessRate Update the recent and cumulative success
            %rate of the Bluetooth channels

            recentSuccessRate = obj.RecentSuccessRates{peripheralIndex};
            overallSuccessRate = obj.OverallSuccessRates{peripheralIndex};
            % Success rate of 0 is added in the advertising channels
            if ~obj.pIsBluetoothBREDR(peripheralIndex)
                recentSuccessRate = [0 recentSuccessRate(1:11) 0 recentSuccessRate(12:end) 0];
                overallSuccessRate = [0 overallSuccessRate(1:11) 0 overallSuccessRate(12:end) 0];
            end
            % Update recent and overall success rate in plot
            obj.pRecentSuccessRateFig{peripheralIndex}.YData = recentSuccessRate;
            obj.pOverallSuccessRateFig{peripheralIndex}.YData = overallSuccessRate;
        end

        function calculateSuccessRate(obj,channelNum,successStatus,peripheralIndex)
            %calculateSuccessRate Calculates the success rate of the
            %Bluetooth channels when the channel classification is
            %disabled

            numClassifications = obj.NumClassifications(peripheralIndex);
            % Update the reception and failure count
            obj.pRxCount{peripheralIndex}(numClassifications+1,channelNum) = ...
                obj.pRxCount{peripheralIndex}(numClassifications+1,channelNum)+1;
            if ~successStatus
                obj.pRxFailureCount{peripheralIndex}(numClassifications+1,channelNum) = ...
                    obj.pRxFailureCount{peripheralIndex}(numClassifications+1,channelNum)+1;
            end
            % Success rate
            rxCount = obj.pRxCount{peripheralIndex}(numClassifications+1,channelNum);
            rxFailureCount = obj.pRxFailureCount{peripheralIndex}(numClassifications+1,channelNum);
            obj.OverallSuccessRates{peripheralIndex}(channelNum) = round(((rxCount-rxFailureCount)/rxCount)*100);
            if numClassifications
                rxCount = rxCount-obj.pRxCount{peripheralIndex}(numClassifications,channelNum);
                rxFailureCount =  rxFailureCount-obj.pRxFailureCount{peripheralIndex}(numClassifications,channelNum);
            end
            obj.RecentSuccessRates{peripheralIndex}(channelNum) = round(((rxCount-rxFailureCount)/rxCount)*100);
        end

        function displayPerformanceGraph(obj,peripheralNode,peripheralIndex,numClassifications)
            %displayPerformanceGraph Creates the packet loss ratio and
            %throughput bar graph

            % Create labels for the performance graph
            xAxisLabels = [string(1:numClassifications) "Till Simulation End"];
            % Plot the packet loss ratio and throughput graph
            figHandle = figure;
            matlab.graphics.internal.themes.figureUseDesktopTheme(figHandle);
            subplot(2,1,1);
            bar(1:numClassifications+1,obj.PacketLossRatio(peripheralIndex,1:numClassifications+1));
            xlabel("Classification");
            ylabel("Packet Loss Ratio");
            set(gca,"xticklabel",xAxisLabels,"xtick",1:numClassifications+1);
            subplot(2,1,2);
            bar(1:numClassifications+1,obj.Throughput(peripheralIndex,1:numClassifications+1));
            xlabel("Classification");
            ylabel("Throughput In Kbps");
            set(gca,"xticklabel",xAxisLabels,"xtick",1:numClassifications+1);
            sgtitle(["Performance for Each Channel Classification of " peripheralNode.Name],"FontSize",12);
        end

        function performanceStatistics(obj,centralNode,peripheralNode,peripheralIndex)
            %performanceStatistics Calculates the packet loss ratio and
            %throughput for the time since last classification

            % Calculate the packet loss ratio and throughput
            numClassifications = obj.NumClassifications(peripheralIndex);
            if obj.pIsBluetoothBREDR(peripheralIndex)
                layer = "Baseband";
            else
                layer = "LL";
            end
            obj.PacketLossRatio(peripheralIndex,numClassifications+1) = ...
                kpi(centralNode,peripheralNode,"PLR",Layer=layer);
            obj.Throughput(peripheralIndex,numClassifications+1) = ...
                kpi(centralNode,peripheralNode,"throughput",Layer=layer);
        end

        function verifyConnection(obj,centralNode,peripheralNode,peripheralIndex)
            %verifyConnection Verifies the connectivity between Central and
            %Peripheral node

            validateattributes(centralNode,["bluetoothLENode","bluetoothNode"],{'scalar'});
            validateattributes(peripheralNode,["bluetoothLENode","bluetoothNode"],{'scalar'});
            if ~strcmp(class(centralNode),class(peripheralNode))
                error("The Central Node and Peripheral node must be of same class.");
            end
            if obj.pIsBluetoothBREDR(peripheralIndex)
                [centralLTAddress{1:numel(centralNode.ConnectionConfig)}] = deal(centralNode.ConnectionConfig.PrimaryLTAddress);
                flag = find(cell2mat(centralLTAddress)== peripheralNode.ConnectionConfig.PrimaryLTAddress);
            else
                flag = false;
                [centralAccessAddress{1:numel(centralNode.ConnectionConfig)}] = deal(centralNode.ConnectionConfig.AccessAddress);
                peripheralAccessAddress = peripheralNode.ConnectionConfig.AccessAddress;
                for idx = 1:numel(centralNode.ConnectionConfig)
                    if strcmpi(centralAccessAddress{idx},peripheralAccessAddress)
                        flag = true;
                        break;
                    end
                end
            end
            if isempty(flag) || flag==false
                error("The Central node must be connected to the Peripheral node.");
            end
        end

        function UpdateProgressBarAtEnd(obj,varargin)
            %UpdateProgressBarAtEnd Update the simulation progress bar at
            %the end of simulation

            for idx = 1:obj.PeripheralCount
                obj.pProgressFig{idx}.Position(3) = obj.ProgressBarDim(3);
                obj.pPercentageFig{idx}.String = '100%';
            end
        end
    end
end