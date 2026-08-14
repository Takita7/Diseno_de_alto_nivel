%% =========================================================================
%  EQUIPO 5 — Comparación de Métricas: 3 Escenarios Bluetooth BR/EDR
%  Curso: MP-6159 Interfaces de Comunicaciones — ITCR
%
%  Escenario 1: Sin interferencia WLAN, sin AFH  (baseline)
%  Escenario 2: Con interferencia WLAN, sin AFH  (impacto)
%  Escenario 3: Con interferencia WLAN, con AFH  (mitigación)
%
%  MÉTRICAS NORMALIZADAS QUE PRODUCE ESTE SCRIPT:
%    1. PER (Packet Error Rate) a nivel PHY
%    2. Throughput de aplicación (Kbps)
%    3. Packet Loss Ratio a nivel de aplicación
%    4. Eficiencia espectral (Kbps/MHz)
% =========================================================================

clear; clc; close all;

%% -------------------------------------------------------------------------
%  PARÁMETROS COMUNES A LOS TRES ESCENARIOS
%  Modificar aquí si quieren variar condiciones
% -------------------------------------------------------------------------

simulationTime   = 1.5;     % segundos — igual que main_simulation.m
bbFileName       = 'WLANHESUBandwidth20.bb';
btBandwidth_MHz  = 1;       % MHz por canal Bluetooth BR

% Localizar archivo .bb (mismo mecanismo que main_simulation.m)
if isfile(fullfile(pwd, bbFileName))
    bbFilePath = fullfile(pwd, bbFileName);
elseif ~isempty(which(bbFileName))
    bbFilePath = which(bbFileName);
else
    results = dir(fullfile(matlabroot, '**', bbFileName));
    if isempty(results)
        error('Archivo %s no encontrado. Copiarlo a la carpeta del proyecto.', bbFileName);
    end
    bbFilePath = fullfile(results(1).folder, results(1).name);
end

%% -------------------------------------------------------------------------
%  CORRER LOS TRES ESCENARIOS
% -------------------------------------------------------------------------

fprintf('Corriendo los 3 escenarios...\n\n');

[s1_central, s1_peripheral] = runScenario(false, false, bbFilePath, simulationTime);
fprintf('  Escenario 1 completado (sin WLAN, sin AFH)\n');

[s2_central, s2_peripheral] = runScenario(true,  false, bbFilePath, simulationTime);
fprintf('  Escenario 2 completado (con WLAN, sin AFH)\n');

[s3_central, s3_peripheral] = runScenario(true,  true,  bbFilePath, simulationTime);
fprintf('  Escenario 3 completado (con WLAN, con AFH)\n\n');

%% -------------------------------------------------------------------------
%  CALCULAR MÉTRICAS — NODO PERIFÉRICO (receptor principal)
% -------------------------------------------------------------------------

% --- Métrica 1: PER a nivel PHY (Packet Error Rate) ---
% PER = DecodeFailures / ReceivedPackets × 100%
% Mide cuántos paquetes llegaron pero no pudieron decodificarse correctamente

per_s1 = (s1_peripheral.PHY.DecodeFailures / s1_peripheral.PHY.ReceivedPackets) * 100;
per_s2 = (s2_peripheral.PHY.DecodeFailures / s2_peripheral.PHY.ReceivedPackets) * 100;
per_s3 = (s3_peripheral.PHY.DecodeFailures / s3_peripheral.PHY.ReceivedPackets) * 100;

% --- Métrica 2: Throughput de aplicación (Kbps) ---
% Bytes recibidos exitosamente a nivel de aplicación
throughput_s1 = (s1_peripheral.App.ReceivedBytes * 8) / (simulationTime * 1e3);
throughput_s2 = (s2_peripheral.App.ReceivedBytes * 8) / (simulationTime * 1e3);
throughput_s3 = (s3_peripheral.App.ReceivedBytes * 8) / (simulationTime * 1e3);

% --- Métrica 3: Packet Loss Ratio a nivel de aplicación ---
% PLR = (Transmitidos - Recibidos) / Transmitidos × 100%
plr_s1 = ((s1_central.App.TransmittedPackets - s1_peripheral.App.ReceivedPackets) / ...
           s1_central.App.TransmittedPackets) * 100;
plr_s2 = ((s2_central.App.TransmittedPackets - s2_peripheral.App.ReceivedPackets) / ...
           s2_central.App.TransmittedPackets) * 100;
plr_s3 = ((s3_central.App.TransmittedPackets - s3_peripheral.App.ReceivedPackets) / ...
           s3_central.App.TransmittedPackets) * 100;

% --- Métrica 4: Eficiencia espectral (Kbps/MHz) ---
spectralEff_s1 = throughput_s1 / btBandwidth_MHz;
spectralEff_s2 = throughput_s2 / btBandwidth_MHz;
spectralEff_s3 = throughput_s3 / btBandwidth_MHz;

%% -------------------------------------------------------------------------
%  IMPRIMIR RESUMEN DE MÉTRICAS NORMALIZADAS
% -------------------------------------------------------------------------

etiquetas = {'Sin WLAN (baseline)', 'Con WLAN sin AFH', 'Con WLAN + AFH'};

fprintf('==================================\n');
fprintf('  MÉTRICAS NORMALIZADAS  BR/EDR\n');
fprintf('==================================\n\n');

fprintf('%-30s %10s %16s %14s\n', '', 'Escenario 1', 'Escenario 2', 'Escenario 3');
fprintf('%-30s %10s %16s %14s\n', '', etiquetas{1}, etiquetas{2}, etiquetas{3});
fprintf('%s\n', repmat('-', 1, 75));

fprintf('%-30s %10.2f %16.2f %14.2f\n', '1. PER PHY (%)',         per_s1,           per_s2,           per_s3);
fprintf('%-30s %10.2f %16.2f %14.2f\n', '2. Throughput (Kbps)',   throughput_s1,    throughput_s2,    throughput_s3);
fprintf('%-30s %10.2f %16.2f %14.2f\n', '3. PLR aplicación (%)',  plr_s1,           plr_s2,           plr_s3);
fprintf('%-30s %10.2f %16.2f %14.2f\n', '4. Ef. espectral Kb/MHz',spectralEff_s1,  spectralEff_s2,   spectralEff_s3);
fprintf('%s\n\n', repmat('-', 1, 75));

% Mejora del AFH respecto a sin AFH
mejora_per       = per_s2 - per_s3;
mejora_throughput = throughput_s3 - throughput_s2;
fprintf('  Reducción de PER con AFH:       %.2f puntos porcentuales\n', mejora_per);
fprintf('  Ganancia de throughput con AFH: %.2f Kbps\n\n', mejora_throughput);

%% -------------------------------------------------------------------------
%  FIGURA 1: PER POR ESCENARIO (figura principal del video)
% -------------------------------------------------------------------------

figure('Name', 'Comparación PER - Bluetooth BR/EDR con Interferencia WLAN', ...
       'Position', [100 100 800 500]);

colores = [0.2 0.6 0.2;   % verde — baseline
           0.8 0.2 0.2;   % rojo   — con WLAN sin AFH
           0.2 0.4 0.8];  % azul   — con WLAN + AFH

valores_per = [per_s1, per_s2, per_s3];
b = bar(valores_per, 0.5, 'FaceColor', 'flat');
b.CData = colores;

% Etiquetas de valor sobre cada barra
for k = 1:3
    text(k, valores_per(k) + 0.3, sprintf('%.1f%%', valores_per(k)), ...
        'HorizontalAlignment', 'center', 'FontWeight', 'bold', 'FontSize', 11);
end

set(gca, 'XTickLabel', etiquetas, 'FontSize', 10);
ylabel('PER a nivel PHY (%)', 'FontSize', 12);
title({'Bluetooth BR/EDR — Packet Error Rate bajo Interferencia WLAN 2.4 GHz', ...
       sprintf('(Simulación de %.1f s, canal DH1, WLAN en 2.442 y 2.447 GHz)', simulationTime)}, ...
      'FontSize', 12);
ylim([0, max(valores_per) * 1.25]);
grid on; grid minor;
hold on; h1=patch(NaN,NaN,colores(1,:)); h2=patch(NaN,NaN,colores(2,:)); h3=patch(NaN,NaN,colores(3,:));
legend([h1 h2 h3], {'Sin interferencia (baseline)', 'Con WLAN sin mitigación', 'Con WLAN + AFH'}, ...
       'Location', 'northeast', 'FontSize', 10);

%% -------------------------------------------------------------------------
%  FIGURA 2: THROUGHPUT POR ESCENARIO
% -------------------------------------------------------------------------

figure('Name', 'Throughput - Bluetooth BR/EDR', 'Position', [150 150 800 500]);

valores_tp = [throughput_s1, throughput_s2, throughput_s3];
b2 = bar(valores_tp, 0.5, 'FaceColor', 'flat');
b2.CData = colores;

for k = 1:3
    text(k, valores_tp(k) + 1, sprintf('%.1f Kbps', valores_tp(k)), ...
        'HorizontalAlignment', 'center', 'FontWeight', 'bold', 'FontSize', 11);
end

set(gca, 'XTickLabel', etiquetas, 'FontSize', 10);
ylabel('Throughput de aplicación (Kbps)', 'FontSize', 12);
title('Bluetooth BR/EDR — Throughput bajo Interferencia WLAN 2.4 GHz', 'FontSize', 12);
ylim([0, max(valores_tp) * 1.25]);
grid on; grid minor;
hold on; h1=patch(NaN,NaN,colores(1,:)); h2=patch(NaN,NaN,colores(2,:)); h3=patch(NaN,NaN,colores(3,:));
legend([h1 h2 h3], {'Sin interferencia (baseline)', 'Con WLAN sin mitigación', 'Con WLAN + AFH'}, ...
       'Location', 'northeast', 'FontSize', 10);

%% -------------------------------------------------------------------------
%  FIGURA 3: PANEL COMPLETO DE MÉTRICAS NORMALIZADAS (para el video)
% -------------------------------------------------------------------------

figure('Name', 'Panel de Métricas Normalizadas', 'Position', [200 50 1000 600]);

subplot(2, 2, 1);
b3 = bar([per_s1 per_s2 per_s3], 0.6, 'FaceColor', 'flat'); b3.CData = colores;
set(gca, 'XTickLabel', {'Baseline','WLAN sin AFH','WLAN+AFH'}, 'FontSize', 9);
ylabel('%'); title('1. PER PHY'); grid on;

subplot(2, 2, 2);
b4 = bar([throughput_s1 throughput_s2 throughput_s3], 0.6, 'FaceColor', 'flat'); b4.CData = colores;
set(gca, 'XTickLabel', {'Baseline','WLAN sin AFH','WLAN+AFH'}, 'FontSize', 9);
ylabel('Kbps'); title('2. Throughput de aplicación'); grid on;

subplot(2, 2, 3);
b5 = bar([plr_s1 plr_s2 plr_s3], 0.6, 'FaceColor', 'flat'); b5.CData = colores;
set(gca, 'XTickLabel', {'Baseline','WLAN sin AFH','WLAN+AFH'}, 'FontSize', 9);
ylabel('%'); title('3. Packet Loss Ratio (app)'); grid on;

subplot(2, 2, 4);
b6 = bar([spectralEff_s1 spectralEff_s2 spectralEff_s3], 0.6, 'FaceColor', 'flat'); b6.CData = colores;
set(gca, 'XTickLabel', {'Baseline','WLAN sin AFH','WLAN+AFH'}, 'FontSize', 9);
ylabel('Kbps/MHz'); title('4. Eficiencia espectral'); grid on;

sgtitle('Equipo 5 — Bluetooth BR/EDR: Métricas Normalizadas del Proyecto', 'FontSize', 13, 'FontWeight', 'bold');

fprintf('Figuras generadas. Script completado.\n');

%% =========================================================================
%  FUNCIÓN LOCAL: runScenario
%  Inicializa y corre una simulación completa con los parámetros dados.
%  Retorna las estadísticas de los nodos central y periférico.
% =========================================================================

function [centralStats, peripheralStats] = runScenario(enableWLAN, enableAFH, bbFilePath, simTime)

    rng(1, "twister");
    networkSimulator = wirelessNetworkSimulator.init;

    % Nodos Bluetooth
    centralNode = bluetoothNode("central", ...
        Name = "Central Node", Position = [0 0 0]);
    peripheralNode = bluetoothNode("peripheral", ...
        Name = "Peripheral Node", Position = [5 0 0]);

    % Conexión BR/EDR
    connectionConfig = bluetoothConnectionConfig;
    connectionConfig.CentralToPeripheralACLPacketType = "DH1";
    connectionConfig.PeripheralToCentralACLPacketType = "DH1";
    connectionConfig.SCOPacketType   = "HV2";
    connectionConfig.PollInterval    = 10;
    connectionConfig.InstantOffset   = 96;
    connectionConfig.TransmitterPower = 0;
    connectionConfig = configureConnection(connectionConfig, centralNode, peripheralNode);

    % Tráfico de aplicación
    trafficSource = networkTrafficOnOff(OnTime = Inf, DataRate = 200, PacketSize = 27);
    addTrafficSource(centralNode,    trafficSource, DestinationNode = peripheralNode);
    addTrafficSource(peripheralNode, trafficSource, DestinationNode = centralNode);

    % Nodos WLAN (solo si está habilitado)
    if enableWLAN
        wlanNodePositions = [0 7 5; 0 3 0];
        wlanCenterFreq    = [2.442e9; 2.447e9];
        wlanNodes = helperInterferingWLANNode.empty(0, 2);
        for idx = 1:2
            wlanNodes(idx) = helperInterferingWLANNode(...
                WaveformSource   = "BasebandFile", ...
                BasebandFile     = bbFilePath, ...
                Position         = wlanNodePositions(idx,:), ...
                Name             = "WLAN node", ...
                TransmitterPower = 20, ...
                CenterFrequency  = wlanCenterFreq(idx), ...
                Bandwidth        = 20e6, ...
                SignalPeriodicity = 2e-3);
        end
    end

    % Agregar nodos al simulador
    addNodes(networkSimulator, [centralNode peripheralNode]);
    if enableWLAN
        addNodes(networkSimulator, wlanNodes);
    end

    % AFH — clasificación de canales (solo si está habilitado)
    if enableAFH
        classifierObj = helperBluetoothChannelClassification(...
            centralNode, peripheralNode, PERThreshold = 40);
        classifyFcn = @(varargin) classifierObj.classifyChannels;
        scheduleAction(networkSimulator, classifyFcn, [], 0, 250e-3);
    end

    % Correr simulación (sin visualización para mayor velocidad)
    run(networkSimulator, simTime);

    % Retornar estadísticas
    centralStats    = statistics(centralNode);
    peripheralStats = statistics(peripheralNode);

end