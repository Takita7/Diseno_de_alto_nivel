%% =========================================================================
%  EQUIPO 5 — Bluetooth BR/EDR con Interferencia WLAN
%  Curso: MP-6159 Interfaces de Comunicaciones — ITCR
%
%  Basado en el ejemplo oficial de MathWorks:
%  "Bluetooth BR/EDR Data and Voice Communication with WLAN Signal Interference"
%
%  TOOLBOXES REQUERIDOS:
%    - Bluetooth Toolbox
%    - Wireless Network Toolbox
%  TOOLBOX OPCIONAL:
%    - WLAN Toolbox (solo si se usa wlanInterferenceSource = "Generated")
%
%  ARCHIVOS NECESARIOS EN LA MISMA CARPETA:
%    - helperInterferingWLANNode.m
%    - helperBluetoothChannelClassification.m   
%    - helperVisualizeCoexistence.m
%    - WLANHESUBandwidth20.bb                   
% =========================================================================

clear; clc; close all;

%% -------------------------------------------------------------------------
%  SECCIÓN 1: PARÁMETROS GLOBALES
%  Modificar aquí para experimentar en fases posteriores
% -------------------------------------------------------------------------

% Semilla para reproducibilidad (cambiar para múltiples corridas)
rng(1, "twister");

% Tiempo de simulación en segundos
simulationTime = 1.5;

% Flags de control
enableWLANInterference      = true;   % false = solo AWGN, sin interferencia
enableChannelClassification = true;   % false = sin AFH
enableVisualization         = true;   % false = sin gráficas en tiempo real

% Fuente de interferencia WLAN:
%   "BasebandFile" -> usa archivo .bb local (no requiere WLAN Toolbox)
%   "Generated"    -> genera señal con WLAN Toolbox (requiere WLAN Toolbox)
wlanInterferenceSource = "BasebandFile";

%% -------------------------------------------------------------------------
%  SECCIÓN 2: INICIALIZAR SIMULADOR DE RED
% -------------------------------------------------------------------------

networkSimulator = wirelessNetworkSimulator.init;

%% -------------------------------------------------------------------------
%  SECCIÓN 3: CREAR NODOS BLUETOOTH BR/EDR
% -------------------------------------------------------------------------

% Nodo Central (coordenadas en metros: x, y, z)
centralNode = bluetoothNode("central", ...
    Name     = "Central Node", ...
    Position = [0 0 0]);

% Nodo Periférico
peripheralNode = bluetoothNode("peripheral", ...
    Name     = "Peripheral Node", ...
    Position = [5 0 0]);

%% -------------------------------------------------------------------------
%  SECCIÓN 4: CONFIGURAR LA CONEXIÓN BR/EDR
% -------------------------------------------------------------------------

connectionConfig = bluetoothConnectionConfig;
connectionConfig.CentralToPeripheralACLPacketType = "DH1";
connectionConfig.PeripheralToCentralACLPacketType = "DH1";
connectionConfig.SCOPacketType                    = "HV2";
connectionConfig.PollInterval                     = 10;    % segundos
connectionConfig.InstantOffset                    = 96;    % slots
connectionConfig.TransmitterPower                 = 0;     % dBm

% Asignar configuración a ambos nodos
connectionConfig = configureConnection(connectionConfig, centralNode, peripheralNode);

%% -------------------------------------------------------------------------
%  SECCIÓN 5: CONFIGURAR TRÁFICO DE APLICACIÓN (ACL)
% -------------------------------------------------------------------------

trafficSource = networkTrafficOnOff(...
    OnTime     = Inf, ...
    DataRate   = 200, ...   % Kbps
    PacketSize = 27);       % bytes

% Agregar tráfico bidireccional
addTrafficSource(centralNode,    trafficSource, DestinationNode = peripheralNode);
addTrafficSource(peripheralNode, trafficSource, DestinationNode = centralNode);

%% -------------------------------------------------------------------------
%  SECCIÓN 6: CONFIGURAR INTERFERENCIA WLAN
% -------------------------------------------------------------------------

if enableWLANInterference

    % --- Localizar el archivo .bb ---
    % En R2026a este archivo no viene en la instalación base.
    % Debe estar copiado en la carpeta local del proyecto
    bbFileName = 'WLANHESUBandwidth20.bb';

    % Buscar en este orden: carpeta local -> MATLAB path -> instalación
    if isfile(fullfile(pwd, bbFileName))
        bbFilePath = fullfile(pwd, bbFileName);
    elseif ~isempty(which(bbFileName))
        bbFilePath = which(bbFileName);
    else
        results = dir(fullfile(matlabroot, '**', bbFileName));
        if isempty(results)
            error(['Archivo ' bbFileName ' no encontrado.\n' ...
                   'Copiar el archivo .bb a la carpeta del proyecto: ' pwd '\n' ...
                   'Se obtiene ejecutando openExample(''shared_wlan_bluetooth/' ...
                   'BluetoothDataAndVoiceCommWithWLANInterferenceExample'') ' ...
                   'y copiando el .bb de esa carpeta temporal.']);
        end
        bbFilePath = fullfile(results(1).folder, results(1).name);
    end
    fprintf('>>> Archivo .bb: %s\n', bbFilePath);

    % --- Crear nodos WLAN ---
    numWLANNodes      = 2;
    wlanNodePositions = [0 7 5; 0 3 0];        % metros (x, y, z)
    wlanCenterFreq    = [2.442e9; 2.447e9];    % Hz — WLAN canales 7 y 8

    wlanNodes = helperInterferingWLANNode.empty(0, numWLANNodes);

    for idx = 1:numWLANNodes
        wlanNodes(idx) = helperInterferingWLANNode(...
            WaveformSource    = wlanInterferenceSource, ...
            BasebandFile      = bbFilePath, ...
            Position          = wlanNodePositions(idx, :), ...
            Name              = "WLAN node", ...
            TransmitterPower  = 20, ...          % dBm
            CenterFrequency   = wlanCenterFreq(idx), ...
            Bandwidth         = 20e6, ...        % Hz
            SignalPeriodicity = 2e-3);           % segundos
    end

end

%% -------------------------------------------------------------------------
%  SECCIÓN 7: AGREGAR NODOS AL SIMULADOR
% -------------------------------------------------------------------------

bluetoothNodes = [centralNode peripheralNode];
addNodes(networkSimulator, bluetoothNodes);

if enableWLANInterference
    addNodes(networkSimulator, wlanNodes);
end

%% -------------------------------------------------------------------------
%  SECCIÓN 8: CLASIFICACIÓN DE CANALES (AFH)
% -------------------------------------------------------------------------

if enableChannelClassification

    classifierObj = helperBluetoothChannelClassification(...
        centralNode, peripheralNode, PERThreshold = 40);

    classifyFcn = @(varargin) classifierObj.classifyChannels;
    userData    = [];
    callAt      = 0;
    periodicity = 250e-3;   % clasificar cada 250 ms

    scheduleAction(networkSimulator, classifyFcn, userData, callAt, periodicity);

end

%% -------------------------------------------------------------------------
%  SECCIÓN 9: VISUALIZACIÓN
% -------------------------------------------------------------------------

if enableVisualization
    if enableWLANInterference
        coexistenceViz = helperVisualizeCoexistence(simulationTime, bluetoothNodes, wlanNodes);
    else
        coexistenceViz = helperVisualizeCoexistence(simulationTime, bluetoothNodes);
    end
end

%% -------------------------------------------------------------------------
%  SECCIÓN 10: CORRER LA SIMULACIÓN
% -------------------------------------------------------------------------

fprintf('\n>>> Iniciando simulación (%.1f s de red)...\n', simulationTime);
run(networkSimulator, simulationTime);
fprintf('>>> Simulación completada.\n\n');

%% -------------------------------------------------------------------------
%  SECCIÓN 11: RECUPERAR Y MOSTRAR ESTADÍSTICAS
% -------------------------------------------------------------------------

centralStats    = statistics(centralNode);
peripheralStats = statistics(peripheralNode);

fprintf('=== ESTADÍSTICAS NODO CENTRAL ===\n');
disp(centralStats.App);
disp(centralStats.PHY);

fprintf('=== ESTADÍSTICAS NODO PERIFÉRICO ===\n');
disp(peripheralStats.App);
disp(peripheralStats.PHY);

%% -------------------------------------------------------------------------
%  SECCIÓN 12: ESTADÍSTICAS DE CLASIFICACIÓN DE CANALES
% -------------------------------------------------------------------------

if enableChannelClassification && enableVisualization
    bluetoothChannelStats = classificationStatistics(...
        coexistenceViz, centralNode, peripheralNode);
end

%% -------------------------------------------------------------------------
%  FIN DEL SCRIPT
%
%  PRÓXIMOS PASOS (Fases siguientes):
%    - enableWLANInterference = false  -> baseline sin interferencia
%    - enableChannelClassification = false -> con interferencia, sin AFH
%    - enableWLANInterference = true + enableChannelClassification = true -> con AFH
%    - Extraer métricas de BER desde peripheralStats.PHY
% -------------------------------------------------------------------------