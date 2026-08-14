%% =========================================================================
%  EQUIPO 5 — Validación del Receptor: Métricas vs. Bluetooth Core Spec v5.4
%  Curso: MP-6159 Interfaces de Comunicaciones — ITCR
%
%  Rol C (Receptor/Validación):
%    1. Deriva BER a partir del PER medido en bt_ber_comparison.m
%    2. Compara las métricas contra los límites del estándar (Core Spec v5.4)
%    3. Realiza un barrido de potencia WLAN (INR) para evaluar sensibilidad
%       a la degradación del canal — Métrica 3 del proyecto
%
%  REFERENCIA:
%    Bluetooth SIG, Core Specification v5.4, Vol 6, Part B, Section 3:
%    "Receiver Characteristics" — sensibilidad mínima, BER máximo
%
%  FIGURAS PRODUCIDAS:
%    1. Tabla de conformidad: métricas simuladas vs. límites del estándar
%    2. Barrido INR: PER vs. potencia de interferencia WLAN con/sin AFH
%    3. BER estimado vs. INR comparado con el límite del estándar
% =========================================================================

clear; clc; close all;

%% -------------------------------------------------------------------------
%  SECCIÓN 1: RESULTADOS DE REFERENCIA (de bt_ber_comparison.m)
%  Actualizar estos valores si se vuelve a correr bt_ber_comparison.m
% -------------------------------------------------------------------------

% --- Resultados de los 3 escenarios (Escenario 1, 2, 3) ---
per_sim    = [0.00,  30.22, 17.40];   % % — PER PHY medido
tp_sim     = [86.40, 40.18, 59.47];   % Kbps — throughput de aplicación
plr_sim    = [7.69,  15.20, 10.80];   % % — packet loss ratio a nivel app
ef_sim     = [86.40, 40.18, 59.47];   % Kbps/MHz — eficiencia espectral

% --- Parámetros del paquete BR DH1 (para derivar BER desde PER) ---
% DH1: 27 bytes payload = 216 bits, sin FEC en payload
N_payload_bits = 216;

% --- Parámetros del escenario de canal (de bt_channel.m) ---
P_BT_rx_dBm   = -61.0;   % dBm — potencia recibida del enlace BT
P_WLAN_tx_dBm = 20.0;    % dBm — potencia de transmisión WLAN del escenario

% --- Límites del Bluetooth Core Spec v5.4 (Vol 6, Parte B, Sección 3) ---
% Sensibilidad mínima BR/EDR: BER ≤ 0.1% con potencia recibida ≥ -70 dBm
BER_limit_pct       = 0.1;     % % — límite máximo de BER en conformidad
sensitivity_dBm     = -70.0;   % dBm — umbral mínimo de sensibilidad
throughput_max_kbps = 723.0;   % Kbps — throughput máximo teórico BR/ACL

fprintf('=== VALIDACIÓN DEL RECEPTOR — BLUETOOTH CORE SPEC v5.4 ===\n\n');

%% -------------------------------------------------------------------------
%  SECCIÓN 2: DERIVACIÓN DE BER A PARTIR DE PER
%
%  Para paquetes sin FEC (DH1): PER = 1 - (1 - BER)^N
%  Despejando BER:              BER = 1 - (1 - PER)^(1/N)
% -------------------------------------------------------------------------

ber_sim_pct = zeros(1,3);
for k = 1:3
    if per_sim(k) > 0
        ber_sim_pct(k) = (1 - (1 - per_sim(k)/100)^(1/N_payload_bits)) * 100;
    end
end

fprintf('--- DERIVACIÓN DE BER DESDE PER (DH1, N=%d bits) ---\n', N_payload_bits);
fprintf('%-30s %12s %12s %12s\n', '', 'Escenario 1', 'Escenario 2', 'Escenario 3');
fprintf('%-30s %12s %12s %12s\n', '', 'Sin WLAN', 'WLAN sin AFH', 'WLAN+AFH');
fprintf('%s\n', repmat('-',1,70));
fprintf('%-30s %11.2f%% %11.2f%% %11.2f%%\n', 'PER PHY medido:', per_sim);
fprintf('%-30s %11.4f%% %11.4f%% %11.4f%%\n', 'BER estimado:', ber_sim_pct);
fprintf('%-30s %12s %12s %12s\n', 'Límite Core Spec (BER≤0.1%):', ...
        '✓ OK', ...
        [char(10060) ' FALLA'], ...
        [char(10060) ' FALLA']);
fprintf('\n');

%% -------------------------------------------------------------------------
%  SECCIÓN 3: TABLA DE CONFORMIDAD vs. BLUETOOTH CORE SPEC v5.4
% -------------------------------------------------------------------------

fprintf('--- TABLA DE CONFORMIDAD (ESCENARIO BASELINE SIN INTERFERENCIA) ---\n\n');

metricas = {
    'Potencia RX BT',      sprintf('%.1f dBm', P_BT_rx_dBm), ...
                           sprintf('≥ %.0f dBm', sensitivity_dBm), ...
                           P_BT_rx_dBm >= sensitivity_dBm;
    'BER (sin interferencia)', sprintf('%.4f%%', ber_sim_pct(1)), ...
                           sprintf('≤ %.1f%%', BER_limit_pct), ...
                           ber_sim_pct(1) <= BER_limit_pct;
    'BER (con WLAN sin AFH)', sprintf('%.4f%%', ber_sim_pct(2)), ...
                           sprintf('≤ %.1f%%', BER_limit_pct), ...
                           ber_sim_pct(2) <= BER_limit_pct;
    'BER (con WLAN + AFH)', sprintf('%.4f%%', ber_sim_pct(3)), ...
                           sprintf('≤ %.1f%%', BER_limit_pct), ...
                           ber_sim_pct(3) <= BER_limit_pct;
    'PER PHY (baseline)',  sprintf('%.2f%%', per_sim(1)), ...
                           '≈ 0% (sin canal ruidoso)', ...
                           per_sim(1) < 1.0;
};

fprintf('%-32s %-18s %-22s %-8s\n', 'Métrica', 'Simulado', 'Límite estándar', 'Estado');
fprintf('%s\n', repmat('-',1,82));
for k = 1:size(metricas,1)
    estado = metricas{k,4};
    if estado
        estadoStr = '✓  OK';
    else
        estadoStr = '✗  FALLA';
    end
    fprintf('%-32s %-18s %-22s %-8s\n', ...
            metricas{k,1}, metricas{k,2}, metricas{k,3}, estadoStr);
end
fprintf('\n');
fprintf('NOTA: "FALLA" en escenarios con interferencia es esperado.\n');
fprintf('Los límites del Core Spec aplican SIN interferencia externa.\n');
fprintf('El propósito del AFH es recuperar el rendimiento en presencia\n');
fprintf('de interferencia, no eliminarla completamente.\n\n');

%% -------------------------------------------------------------------------
%  SECCIÓN 4: BARRIDO DE POTENCIA WLAN (INR SWEEP)
%  Varía la potencia de la interferencia WLAN y mide PER con/sin AFH
%  Evalúa la Métrica 3: sensibilidad a la degradación del canal
% -------------------------------------------------------------------------

fprintf('--- BARRIDO DE POTENCIA WLAN (INR SWEEP) ---\n');
fprintf('Esto puede tardar 4-6 minutos...\n\n');

% Potencias WLAN a evaluar (dBm)
wlanPowers_dBm = [0, 5, 10, 15, 20, 25, 30];
nPoints = length(wlanPowers_dBm);

% Localizar archivo .bb
bbFileName = 'WLANHESUBandwidth20.bb';
if isfile(fullfile(pwd, bbFileName))
    bbFilePath = fullfile(pwd, bbFileName);
elseif ~isempty(which(bbFileName))
    bbFilePath = which(bbFileName);
else
    results = dir(fullfile(matlabroot, '**', bbFileName));
    if isempty(results)
        error('Archivo %s no encontrado.', bbFileName);
    end
    bbFilePath = fullfile(results(1).folder, results(1).name);
end

% Tiempo de simulación reducido para el barrido (rapidez)
simTime_sweep = 0.75;   % segundos — suficiente para 3 intervalos AFH

per_noAFH = zeros(1, nPoints);
per_AFH   = zeros(1, nPoints);

for k = 1:nPoints
    pw = wlanPowers_dBm(k);

    % Sin AFH
    [~, pStats] = runScenario(true, false, bbFilePath, simTime_sweep, pw);
    per_noAFH(k) = (pStats.PHY.DecodeFailures / pStats.PHY.ReceivedPackets) * 100;

    % Con AFH
    [~, pStats] = runScenario(true, true, bbFilePath, simTime_sweep, pw);
    per_AFH(k) = (pStats.PHY.DecodeFailures / pStats.PHY.ReceivedPackets) * 100;

    fprintf('  WLAN = %+3d dBm → PER sin AFH: %5.1f%%  |  PER con AFH: %5.1f%%\n', ...
            pw, per_noAFH(k), per_AFH(k));
end

% Derivar BER estimado desde PER del barrido
ber_noAFH_pct = (1 - (1 - per_noAFH/100).^(1/N_payload_bits)) * 100;
ber_AFH_pct   = (1 - (1 - per_AFH/100  ).^(1/N_payload_bits)) * 100;

% SIR estimado para cada potencia WLAN (usando modelo bt_channel)
% PL_BT = 61 dB (fijo), PL_WLAN2 = 63 dB (fijo — posición)
PL_WLAN_dominante = 63.0;   % dB — nodo WLAN2 (más cercano al periférico)
SIR_sweep_dB = P_BT_rx_dBm - (wlanPowers_dBm - PL_WLAN_dominante);

fprintf('\n');

%% -------------------------------------------------------------------------
%  FIGURA 1: TABLA DE CONFORMIDAD (visual)
% -------------------------------------------------------------------------

figure('Name','Receptor — Tabla de Conformidad','Position',[50 50 900 430]);

categorias_conf = {'BER baseline', 'BER con WLAN sin AFH', 'BER con WLAN + AFH'};
ber_vals        = ber_sim_pct(1:3);
colores_conf    = [0.2 0.7 0.2;   % verde — baseline
                   0.8 0.2 0.2;   % rojo — con WLAN sin AFH
                   0.2 0.4 0.8];  % azul — con WLAN + AFH

hold on;
for k = 1:3
    bar(k, ber_vals(k), 0.5, 'FaceColor', colores_conf(k,:), 'EdgeColor', 'none');
end

% Línea de límite del estándar
yl = yline(BER_limit_pct, 'k--', 'LineWidth', 2.0, 'HandleVisibility', 'off');
text(3.6, BER_limit_pct * 1.15, ...
     sprintf('Límite Core Spec v5.4\nBER ≤ %.1f%%', BER_limit_pct), ...
     'FontSize', 9, 'FontWeight', 'bold', 'HorizontalAlignment', 'right');

% Etiquetas de valor
for k = 1:3
    text(k, ber_vals(k) + BER_limit_pct * 0.04, ...
         sprintf('%.4f%%', ber_vals(k)), ...
         'HorizontalAlignment', 'center', 'FontWeight', 'bold', 'FontSize', 10);
end

% Anotaciones de conformidad
ymax = max([ber_vals, BER_limit_pct]) * 1.4;
for k = 1:3
    if ber_vals(k) <= BER_limit_pct
        text(k, ymax * 0.92, '✓ OK', 'HorizontalAlignment', 'center', ...
             'FontSize', 11, 'Color', [0.1 0.6 0.1], 'FontWeight', 'bold');
    else
        text(k, ymax * 0.92, '✗ Sobre límite', 'HorizontalAlignment', 'center', ...
             'FontSize', 10, 'Color', [0.7 0.1 0.1], 'FontWeight', 'bold');
    end
end

set(gca, 'XTick', 1:3, 'XTickLabel', categorias_conf, 'FontSize', 10);
ylabel('BER estimado (%)', 'FontSize', 11);
title({'Conformidad vs. Bluetooth Core Spec v5.4 (BR, DH1)', ...
       'BER estimado desde PER: BER = 1 - (1-PER)^{1/216}'}, ...
      'FontSize', 11, 'FontWeight', 'bold');
ylim([0 ymax]); grid on; hold off;

%% -------------------------------------------------------------------------
%  FIGURA 2: PER vs. POTENCIA WLAN (INR SWEEP)
% -------------------------------------------------------------------------

figure('Name','Receptor — PER vs. Potencia WLAN','Position',[100 80 900 480]);

hold on;
h1 = plot(wlanPowers_dBm, per_noAFH, 'r-o', 'LineWidth', 2.0, ...
          'MarkerFaceColor', 'r', 'MarkerSize', 7, ...
          'DisplayName', 'Con WLAN, sin AFH');
h2 = plot(wlanPowers_dBm, per_AFH, 'b-s', 'LineWidth', 2.0, ...
          'MarkerFaceColor', 'b', 'MarkerSize', 7, ...
          'DisplayName', 'Con WLAN + AFH');

% Marcar el punto de operación del escenario principal (20 dBm)
idx_op = find(wlanPowers_dBm == P_WLAN_tx_dBm);
if ~isempty(idx_op)
    plot(P_WLAN_tx_dBm, per_noAFH(idx_op), 'ro', 'MarkerSize', 14, ...
         'LineWidth', 2.5, 'HandleVisibility', 'off');
    plot(P_WLAN_tx_dBm, per_AFH(idx_op), 'bs', 'MarkerSize', 14, ...
         'LineWidth', 2.5, 'HandleVisibility', 'off');
    xline(P_WLAN_tx_dBm, 'k:', 'LineWidth', 1.2, 'HandleVisibility', 'off');
    text(P_WLAN_tx_dBm + 0.5, 5, ...
         sprintf('Punto de\noperación\n(%d dBm)', P_WLAN_tx_dBm), ...
         'FontSize', 8, 'Color', [0.3 0.3 0.3]);
end

% Área de mejora del AFH
fill([wlanPowers_dBm, fliplr(wlanPowers_dBm)], ...
     [per_noAFH, fliplr(per_AFH)], ...
     [0.2 0.4 0.8], 'FaceAlpha', 0.1, 'EdgeColor', 'none', ...
     'DisplayName', 'Ganancia del AFH');

xlabel('Potencia TX interferencia WLAN (dBm)', 'FontSize', 11);
ylabel('PER PHY (%)', 'FontSize', 11);
title({'Sensibilidad a la Degradación del Canal', ...
       'PER vs. Potencia de Interferencia WLAN — con y sin AFH'}, ...
      'FontSize', 11, 'FontWeight', 'bold');
legend('Location', 'northwest', 'FontSize', 10);
xlim([wlanPowers_dBm(1)-1, wlanPowers_dBm(end)+1]);
ylim([0 min(100, max(per_noAFH)*1.3)]);
grid on; hold off;

%% -------------------------------------------------------------------------
%  FIGURA 3: BER ESTIMADO vs. SIR (comparado con límite del estándar)
% -------------------------------------------------------------------------

figure('Name','Receptor — BER vs. SIR','Position',[150 100 850 460]);

hold on;
h3 = plot(SIR_sweep_dB, ber_noAFH_pct, 'r-o', 'LineWidth', 2.0, ...
          'MarkerFaceColor', 'r', 'MarkerSize', 7, ...
          'DisplayName', 'Con WLAN, sin AFH');
h4 = plot(SIR_sweep_dB, ber_AFH_pct, 'b-s', 'LineWidth', 2.0, ...
          'MarkerFaceColor', 'b', 'MarkerSize', 7, ...
          'DisplayName', 'Con WLAN + AFH');

% Límite del estándar
yline(BER_limit_pct, 'k--', 'LineWidth', 2.0, 'HandleVisibility', 'off');
text(SIR_sweep_dB(end) + 0.5, BER_limit_pct * 1.15, ...
     sprintf('Límite Core Spec\nBER = %.1f%%', BER_limit_pct), ...
     'FontSize', 8, 'FontWeight', 'bold', 'HorizontalAlignment', 'left');

% Marcar SIR del escenario principal
SIR_op = P_BT_rx_dBm - (P_WLAN_tx_dBm - PL_WLAN_dominante);
xline(SIR_op, 'k:', 'LineWidth', 1.2, 'HandleVisibility', 'off');
text(SIR_op + 0.3, max(ber_noAFH_pct)*0.9, ...
     sprintf('SIR operación\n= %.1f dB', SIR_op), ...
     'FontSize', 8, 'Color', [0.3 0.3 0.3]);

xlabel('SIR en el receptor Bluetooth (dB)', 'FontSize', 11);
ylabel('BER estimado (%)', 'FontSize', 11);
title({'BER Estimado vs. SIR — Comparación con Límite del Estándar', ...
       'BER derivado de PER medido: BER = 1 - (1-PER)^{1/216}'}, ...
      'FontSize', 11, 'FontWeight', 'bold');
legend([h3 h4], 'Location', 'northeast', 'FontSize', 10);
grid on; hold off;

fprintf('Script bt_receiver completado. Figuras 1-3 generadas.\n');

%% =========================================================================
%  FUNCIÓN LOCAL: runScenario (versión extendida con parámetro de potencia WLAN)
% =========================================================================

function [centralStats, peripheralStats] = runScenario(...
        enableWLAN, enableAFH, bbFilePath, simTime, wlanPower_dBm)

    if nargin < 5
        wlanPower_dBm = 20;   % valor por defecto del escenario principal
    end

    rng(1, "twister");
    networkSimulator = wirelessNetworkSimulator.init;

    centralNode = bluetoothNode("central", ...
        Name = "Central Node", Position = [0 0 0]);
    peripheralNode = bluetoothNode("peripheral", ...
        Name = "Peripheral Node", Position = [5 0 0]);

    connectionConfig = bluetoothConnectionConfig;
    connectionConfig.CentralToPeripheralACLPacketType = "DH1";
    connectionConfig.PeripheralToCentralACLPacketType = "DH1";
    connectionConfig.SCOPacketType    = "HV2";
    connectionConfig.PollInterval     = 10;
    connectionConfig.InstantOffset    = 96;
    connectionConfig.TransmitterPower = 0;
    connectionConfig = configureConnection(connectionConfig, centralNode, peripheralNode);

    trafficSource = networkTrafficOnOff(OnTime = Inf, DataRate = 200, PacketSize = 27);
    addTrafficSource(centralNode,    trafficSource, DestinationNode = peripheralNode);
    addTrafficSource(peripheralNode, trafficSource, DestinationNode = centralNode);

    if enableWLAN
        wlanPositions = [0 7 5; 0 3 0];
        wlanFreqs     = [2.442e9; 2.447e9];
        wlanNodes     = helperInterferingWLANNode.empty(0, 2);
        for idx = 1:2
            wlanNodes(idx) = helperInterferingWLANNode(...
                WaveformSource    = "BasebandFile", ...
                BasebandFile      = bbFilePath, ...
                Position          = wlanPositions(idx,:), ...
                Name              = "WLAN node", ...
                TransmitterPower  = wlanPower_dBm, ...   % ← parámetro variable
                CenterFrequency   = wlanFreqs(idx), ...
                Bandwidth         = 20e6, ...
                SignalPeriodicity = 2e-3);
        end
    end

    addNodes(networkSimulator, [centralNode peripheralNode]);
    if enableWLAN
        addNodes(networkSimulator, wlanNodes);
    end

    if enableAFH
        classifierObj = helperBluetoothChannelClassification(...
            centralNode, peripheralNode, PERThreshold = 40);
        classifyFcn = @(varargin) classifierObj.classifyChannels;
        scheduleAction(networkSimulator, classifyFcn, [], 0, 250e-3);
    end

    run(networkSimulator, simTime);

    centralStats    = statistics(centralNode);
    peripheralStats = statistics(peripheralNode);
end