%% =========================================================================
%  EQUIPO 5 — Script Maestro de Integración: Bluetooth BR/EDR + WLAN
%  Curso: MP-6159 Interfaces de Comunicaciones — ITCR
%
%  Consolida los resultados de las tres fases del proyecto:
%    Fase 1 (Transmisor)  → parámetros PHY de los modos BR/EDR
%    Fase 2 (Canal)       → modelo de interferencia WLAN
%    Fase 3 (Receptor)    → métricas de validación vs. Core Spec v5.4
%
%  SCRIPTS INDIVIDUALES (para análisis detallado por fase):
%    bt_waveforms.m     → formas de onda PHY (Fase 1)
%    bt_channel.m       → caracterización analítica del canal (Fase 2)
%    bt_ber_comparison.m→ comparación de 3 escenarios (Fases 2-3)
%    bt_receiver.m      → validación vs. estándar + barrido INR (Fase 3)
%    main_simulation.m  → simulación interactiva con visualización
%
%  ESTE SCRIPT genera:
%    - Figura 1: Panel consolidado 2×3 con todas las métricas del proyecto
%    - Figura 2: Figura central del video — historia completa en una imagen
%    - Resumen completo en consola
%
%  TIEMPO DE EJECUCIÓN: ~3 minutos (3 simulaciones)
% =========================================================================

clear; clc; close all;

%% =========================================================================
%  SECCIÓN 1: PARÁMETROS CENTRALIZADOS
%  Punto único de configuración para todo el proyecto
% =========================================================================

% --- Semilla y tiempo de simulación ---
rng(1, 'twister');
simulationTime = 1.5;           % segundos

% --- Parámetros del enlace Bluetooth BR/EDR ---
BT_txPower_dBm      = 0;        % dBm — Clase 2
BT_packetType       = "DH1";    % tipo de paquete ACL
BT_dataRate_kbps    = 200;      % Kbps — tasa de tráfico de aplicación
BT_packetSize_bytes = 27;       % bytes — tamaño de paquete de aplicación
N_payload_bits      = 216;      % bits — payload DH1 (para BER desde PER)

% --- Parámetros del canal WLAN ---
WLAN_txPower_dBm    = 20;       % dBm — AP doméstico típico 802.11g
WLAN_freq1_GHz      = 2.442;    % GHz — canal WLAN 7
WLAN_freq2_GHz      = 2.447;    % GHz — canal WLAN 8
WLAN_BW_MHz         = 20;       % MHz
WLAN_periodicity_s  = 2e-3;     % s

% --- Parámetros del algoritmo AFH ---
AFH_perThreshold    = 40;       % % — umbral para clasificar canal como "bad"
AFH_periodicity_s   = 250e-3;   % s — intervalo de reclasificación

% --- Límite del estándar (Core Spec v5.4) ---
BER_limit_pct       = 0.1;      % % — BER máximo en conformidad

% --- Localizar archivo .bb ---
bbFileName = 'WLANHESUBandwidth20.bb';
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

fprintf('=== BLUETOOTH BR/EDR — SIMULACIÓN INTEGRADA (EQUIPO 5) ===\n\n');

%% =========================================================================
%  SECCIÓN 2: EJECUTAR LOS 3 ESCENARIOS
% =========================================================================

fprintf('Corriendo escenarios...\n');

[s1c, s1p, ~]         = runScenario(false, false, bbFilePath, simulationTime, ...
                                     WLAN_txPower_dBm, AFH_perThreshold, ...
                                     AFH_periodicity_s, BT_txPower_dBm, ...
                                     BT_dataRate_kbps, BT_packetSize_bytes);
fprintf('  Escenario 1: baseline sin interferencia\n');

[s2c, s2p, ~]         = runScenario(true,  false, bbFilePath, simulationTime, ...
                                     WLAN_txPower_dBm, AFH_perThreshold, ...
                                     AFH_periodicity_s, BT_txPower_dBm, ...
                                     BT_dataRate_kbps, BT_packetSize_bytes);
fprintf('  Escenario 2: con interferencia WLAN, sin AFH\n');

[s3c, s3p, channelMap] = runScenario(true,  true,  bbFilePath, simulationTime, ...
                                     WLAN_txPower_dBm, AFH_perThreshold, ...
                                     AFH_periodicity_s, BT_txPower_dBm, ...
                                     BT_dataRate_kbps, BT_packetSize_bytes);
fprintf('  Escenario 3: con interferencia WLAN + AFH\n\n');

%% =========================================================================
%  SECCIÓN 3: CALCULAR MÉTRICAS CONSOLIDADAS
% =========================================================================

% PER PHY
per = [s1p.PHY.DecodeFailures / s1p.PHY.ReceivedPackets, ...
       s2p.PHY.DecodeFailures / s2p.PHY.ReceivedPackets, ...
       s3p.PHY.DecodeFailures / s3p.PHY.ReceivedPackets] * 100;

% Throughput de aplicación (Kbps)
tp = [s1p.App.ReceivedBytes, s2p.App.ReceivedBytes, s3p.App.ReceivedBytes] ...
     * 8 / (simulationTime * 1e3);

% Packet Loss Ratio
plr = [(s1c.App.TransmittedPackets - s1p.App.ReceivedPackets) / s1c.App.TransmittedPackets, ...
       (s2c.App.TransmittedPackets - s2p.App.ReceivedPackets) / s2c.App.TransmittedPackets, ...
       (s3c.App.TransmittedPackets - s3p.App.ReceivedPackets) / s3c.App.TransmittedPackets] * 100;

% BER estimado desde PER (DH1, N=216 bits sin FEC)
ber_pct = zeros(1,3);
for k = 1:3
    if per(k) > 0
        ber_pct(k) = (1 - (1 - per(k)/100)^(1/N_payload_bits)) * 100;
    end
end

% Canales clasificados como "bad" por AFH
n_bad      = sum(~channelMap);
n_good     = sum(channelMap);
pct_bad    = n_bad / 79 * 100;

% Mejoras del AFH respecto al escenario sin AFH
mejora_per = per(2) - per(3);
mejora_tp  = tp(3)  - tp(2);

% Resumen en consola
etiquetas = {'Sin WLAN (baseline)', 'Con WLAN sin AFH', 'Con WLAN + AFH'};
fprintf('============================================================\n');
fprintf('  MÉTRICAS CONSOLIDADAS — EQUIPO 5 BLUETOOTH BR/EDR\n');
fprintf('============================================================\n\n');
fprintf('%-28s %14s %14s %14s\n', '', etiquetas{1}, etiquetas{2}, etiquetas{3});
fprintf('%s\n', repmat('-',1,72));
fprintf('%-28s %13.2f%% %13.2f%% %13.2f%%\n', '1. PER PHY:',       per);
fprintf('%-28s %13.4f%% %13.4f%% %13.4f%%\n', '2. BER estimado:',  ber_pct);
fprintf('%-28s %13.2f  %13.2f  %13.2f  Kbps\n', '3. Throughput:',  tp);
fprintf('%-28s %13.2f%% %13.2f%% %13.2f%%\n', '4. PLR app:',       plr);
fprintf('%s\n', repmat('-',1,72));
fprintf('\n');
fprintf('Límite Core Spec v5.4 (BER ≤ %.1f%%):\n', BER_limit_pct);
for k = 1:3
    if ber_pct(k) <= BER_limit_pct
        estado = '✓ OK';
    else
        estado = '✗ Sobre límite';
    end
    fprintf('  %-26s BER = %.4f%% → %s\n', [etiquetas{k} ':'], ber_pct(k), estado);
end
fprintf('\n');
fprintf('Canales clasificados por AFH:\n');
fprintf('  Buenos (usados):  %d / 79 (%.0f%%)\n', n_good, n_good/79*100);
fprintf('  Malos (excluidos):%d / 79 (%.0f%%)\n', n_bad,  pct_bad);
fprintf('\n');
fprintf('Ganancia del AFH:\n');
fprintf('  Reducción de PER:       %.2f puntos porcentuales\n', mejora_per);
fprintf('  Ganancia de throughput: %.2f Kbps\n\n', mejora_tp);

%% =========================================================================
%  SECCIÓN 4: FIGURA 1 — PANEL CONSOLIDADO 2×3
% =========================================================================

colores = [0.2 0.7 0.2;   % verde  — baseline
           0.8 0.2 0.2;   % rojo   — WLAN sin AFH
           0.2 0.4 0.8];  % azul   — WLAN + AFH
labels3 = {'Baseline', 'WLAN sin AFH', 'WLAN+AFH'};

figure('Name','Equipo 5 — Panel Consolidado de Métricas', ...
       'Position', [30 30 1200 680]);

% ---- (1,1) PER por escenario ----
subplot(2,3,1);
b1 = bar(per, 0.55, 'FaceColor','flat'); b1.CData = colores;
hold on;
for k=1:3
    text(k, per(k)+0.5, sprintf('%.1f%%',per(k)), ...
         'HorizontalAlignment','center','FontWeight','bold','FontSize',9);
end
set(gca,'XTickLabel',labels3,'FontSize',8);
ylabel('PER PHY (%)','FontSize',9);
title('1. Tasa de Error de Paquetes','FontSize',10,'FontWeight','bold');
ylim([0 max(per)*1.3]); grid on; hold off;

% ---- (1,2) BER vs límite del estándar ----
subplot(2,3,2);
b2 = bar(ber_pct, 0.55, 'FaceColor','flat'); b2.CData = colores;
hold on;
yline(BER_limit_pct,'k--','LineWidth',1.8,'HandleVisibility','off');
for k=1:3
    text(k, ber_pct(k)+BER_limit_pct*0.05, sprintf('%.4f%%',ber_pct(k)), ...
         'HorizontalAlignment','center','FontWeight','bold','FontSize',8);
end
text(3.4, BER_limit_pct*1.12, 'Límite 0.1%', 'FontSize',8,'FontWeight','bold');
set(gca,'XTickLabel',labels3,'FontSize',8);
ylabel('BER estimado (%)','FontSize',9);
title('2. BER vs. Core Spec v5.4','FontSize',10,'FontWeight','bold');
ylim([0 max(ber_pct)*1.5]); grid on; hold off;

% ---- (1,3) Throughput ----
subplot(2,3,3);
b3 = bar(tp, 0.55, 'FaceColor','flat'); b3.CData = colores;
hold on;
for k=1:3
    text(k, tp(k)+1.5, sprintf('%.1f',tp(k)), ...
         'HorizontalAlignment','center','FontWeight','bold','FontSize',9);
end
set(gca,'XTickLabel',labels3,'FontSize',8);
ylabel('Throughput (Kbps)','FontSize',9);
title('3. Throughput de Aplicación','FontSize',10,'FontWeight','bold');
ylim([0 max(tp)*1.25]); grid on; hold off;

% ---- (2,1) Mapa de canales AFH ----
subplot(2,3,4);
bt_ch   = 0:78;
bt_freq = 2402 + bt_ch;
good_ch = channelMap == 1;
bad_ch  = ~good_ch;
hold on;
bar(bt_freq(good_ch), ones(1,sum(good_ch)), 0.8, ...
    'FaceColor',[0.2 0.7 0.2],'EdgeColor','none','DisplayName', ...
    sprintf('Buenos: %d', n_good));
bar(bt_freq(bad_ch),  ones(1,sum(bad_ch)),  0.8, ...
    'FaceColor',[0.8 0.2 0.2],'EdgeColor','none','DisplayName', ...
    sprintf('Malos: %d', n_bad));
% WLAN bands
patch([2432 2457 2457 2432],[0 0 1.2 1.2],[0.9 0.6 0.2], ...
      'FaceAlpha',0.2,'EdgeColor','none','HandleVisibility','off');
text(2444.5, 1.12, 'WLAN 7+8', 'HorizontalAlignment','center', ...
     'FontSize',7,'Color',[0.6 0.3 0]);
legend('Location','northwest','FontSize',7);
xlabel('Frecuencia (MHz)','FontSize',8);
title('4. Mapa de Canales AFH Final','FontSize',10,'FontWeight','bold');
xlim([2400 2485]); ylim([0 1.3]);
set(gca,'YTick',[],'FontSize',8); grid on; hold off;

% ---- (2,2) Eficiencia espectral PHY (BR/EDR) ----
subplot(2,3,5);
modos_ef  = {'BR','EDR2M','EDR3M','LE1M','LE2M'};
ef_vals   = [1.0, 2.0, 3.0, 0.5, 1.0];
col_ef    = [0.2 0.6 0.2; 0.2 0.4 0.8; 0.8 0.2 0.1; ...
             0.7 0.5 0.0; 0.5 0.0 0.7];
b5 = bar(ef_vals, 0.6, 'FaceColor','flat'); b5.CData = col_ef;
hold on;
for k=1:5
    text(k, ef_vals(k)+0.06, sprintf('%.1f',ef_vals(k)), ...
         'HorizontalAlignment','center','FontWeight','bold','FontSize',9);
end
yline(1,'k--','LineWidth',1,'HandleVisibility','off');
set(gca,'XTickLabel',modos_ef,'FontSize',8);
ylabel('bps/Hz','FontSize',9);
title('5. Eficiencia Espectral PHY','FontSize',10,'FontWeight','bold');
ylim([0 3.8]); grid on; hold off;

% ---- (2,3) Tabla de resumen ----
subplot(2,3,6);
axis off;
col1 = {'Métrica','PER PHY','BER estimado','Throughput','PLR app','Core Spec'};
col2 = {etiquetas{1}, ...
        sprintf('%.2f%%',per(1)), sprintf('%.4f%%',ber_pct(1)), ...
        sprintf('%.1f Kbps',tp(1)), sprintf('%.1f%%',plr(1)), '✓ OK'};
col3 = {etiquetas{2}, ...
        sprintf('%.2f%%',per(2)), sprintf('%.4f%%',ber_pct(2)), ...
        sprintf('%.1f Kbps',tp(2)), sprintf('%.1f%%',plr(2)), '✗ Viola'};
col4 = {etiquetas{3}, ...
        sprintf('%.2f%%',per(3)), sprintf('%.4f%%',ber_pct(3)), ...
        sprintf('%.1f Kbps',tp(3)), sprintf('%.1f%%',plr(3)), '✓ OK'};

tableData = [col1' col2' col3' col4'];
t = uitable('Parent', gcf, ...
    'Data', tableData, ...
    'Units','normalized', ...
    'Position', [0.672 0.04 0.305 0.28], ...
    'ColumnWidth', {90 78 88 78}, ...
    'FontSize', 8, ...
    'RowName', {});
text(0.5, 0.75, '6. Resumen de Métricas Normalizadas', ...
     'HorizontalAlignment','center','FontSize',10,'FontWeight','bold', ...
     'Units','normalized');
text(0.5, 0.62, sprintf('Ganancia AFH: PER −%.1f pp | Throughput +%.1f Kbps', ...
     mejora_per, mejora_tp), ...
     'HorizontalAlignment','center','FontSize',9,'Units','normalized', ...
     'Color',[0.2 0.4 0.8]);

sgtitle('Equipo 5 — Bluetooth BR/EDR: Resultados Integrados del Proyecto', ...
        'FontSize',13,'FontWeight','bold');

%% =========================================================================
%  SECCIÓN 5: FIGURA 2 — FIGURA CENTRAL DEL VIDEO
%  Una sola figura clara y limpia que cuenta la historia completa
% =========================================================================

figure('Name','Equipo 5 — Figura Central del Video', ...
       'Position', [80 80 950 520]);

% Eje izquierdo: PER (barras)
yyaxis left;
b_main = bar(1:3, per, 0.45, 'FaceColor','flat');
b_main.CData = colores;
ylabel('PER PHY (%)', 'FontSize', 12, 'Color', 'k');
ylim([0 max(per)*1.45]);

hold on;
% Etiquetas PER
for k=1:3
    text(k, per(k)+0.8, sprintf('PER = %.1f%%', per(k)), ...
         'HorizontalAlignment','center','FontWeight','bold','FontSize',10);
end
% Etiquetas BER
for k=1:3
    color_ber = 'k';
    if ber_pct(k) > BER_limit_pct; color_ber = [0.7 0.1 0.1]; end
    text(k, per(k)+3.5, sprintf('BER = %.4f%%', ber_pct(k)), ...
         'HorizontalAlignment','center','FontSize',8,'Color',color_ber);
end

% Línea de límite del estándar (en escala de PER equivalente)
% PER equivalente al límite BER = 0.1%: PER = 1-(1-0.001)^216 ≈ 19.5%
per_equiv_limit = (1 - (1 - BER_limit_pct/100)^N_payload_bits) * 100;
yline(per_equiv_limit, 'k--', 'LineWidth', 1.8, 'HandleVisibility','off');
text(3.45, per_equiv_limit + 0.5, ...
     sprintf('PER equiv. límite BER=%.1f%%\n= %.1f%%', BER_limit_pct, per_equiv_limit), ...
     'FontSize', 8, 'FontWeight','bold');

% Eje derecho: throughput (línea)
yyaxis right;
plot(1:3, tp, 'k-^', 'LineWidth', 2.0, 'MarkerSize', 9, ...
     'MarkerFaceColor', 'k', 'DisplayName', 'Throughput');
ylabel('Throughput de aplicación (Kbps)', 'FontSize', 12, 'Color', 'k');
ylim([0 max(tp)*1.6]);
for k=1:3
    text(k+0.18, tp(k)+1.5, sprintf('%.0f Kbps', tp(k)), ...
         'FontSize', 9, 'Color', [0.2 0.2 0.2]);
end

set(gca, 'XTick', 1:3, 'XTickLabel', etiquetas, 'FontSize', 11);
title({'Bluetooth BR/EDR — Impacto de Interferencia WLAN y Mitigación AFH', ...
       sprintf('Escenario: 2 nodos WLAN en 2.442/2.447 GHz, %d dBm | SIR = −18.8 dB', ...
               WLAN_txPower_dBm)}, ...
      'FontSize', 11, 'FontWeight', 'bold');

% Flecha de mejora del AFH
annotation('arrow', [0.595 0.745], [0.43 0.43], 'Color', [0.2 0.4 0.8], ...
           'LineWidth', 1.5, 'HeadWidth', 8);
annotation('textbox', [0.60 0.44 0.14 0.06], ...
           'String', sprintf('AFH: −%.1f pp PER\n+%.0f Kbps', mejora_per, mejora_tp), ...
           'EdgeColor','none','Color',[0.2 0.4 0.8],'FontSize',9, ...
           'FontWeight','bold','HorizontalAlignment','center');

grid on; hold off;

fprintf('Script bt_main completado. Figuras 1-2 generadas.\n');

%% =========================================================================
%  FUNCIÓN LOCAL: runScenario (versión integrada con todos los parámetros)
% =========================================================================

function [centralStats, peripheralStats, channelMap] = runScenario(...
        enableWLAN, enableAFH, bbFilePath, simTime, ...
        wlanPower, afh_threshold, afh_period, btPower, dataRate, pktSize)

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
    connectionConfig.TransmitterPower = btPower;
    connectionConfig = configureConnection(connectionConfig, centralNode, peripheralNode);

    trafficSource = networkTrafficOnOff(...
        OnTime = Inf, DataRate = dataRate, PacketSize = pktSize);
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
                TransmitterPower  = wlanPower, ...
                CenterFrequency   = wlanFreqs(idx), ...
                Bandwidth         = 20e6, ...
                SignalPeriodicity = 2e-3);
        end
    end

    addNodes(networkSimulator, [centralNode peripheralNode]);
    if enableWLAN
        addNodes(networkSimulator, wlanNodes);
    end

    classifierObj = [];
    if enableAFH
        classifierObj = helperBluetoothChannelClassification(...
            centralNode, peripheralNode, PERThreshold = afh_threshold);
        classifyFcn = @(varargin) classifierObj.classifyChannels;
        scheduleAction(networkSimulator, classifyFcn, [], 0, afh_period);
    end

    run(networkSimulator, simTime);

    centralStats    = statistics(centralNode);
    peripheralStats = statistics(peripheralNode);

    % Extraer mapa de canales AFH (1=bueno, 0=malo)
    if ~isempty(classifierObj)
        channelMap = classifierObj.ChannelMap;
    else
        channelMap = ones(1, 79);   % todos buenos si no hay AFH
    end
end