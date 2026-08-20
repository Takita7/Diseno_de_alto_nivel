%% =========================================================================
%  EQUIPO 5 — Caracterización del Canal: Interferencia WLAN en 2.4 GHz
%  Curso: MP-6159 Interfaces de Comunicaciones — ITCR
%
%  Rol B (Canal): documenta y justifica cuantitativamente el escenario
%  de canal usando el modelo de pérdida de trayecto indoor (IEEE 802.15.2)
%
%  REFERENCIA PRINCIPAL:
%    IEEE 802.15.2-2003, "Coexistence of Wireless Personal Area Networks
%    with Other Wireless Devices Operating in Unlicensed Frequency Bands"
%
%  FIGURAS PRODUCIDAS:
%    1. Plan de frecuencias 2.4 GHz — solapamiento BT vs WLAN
%    2. Pérdida de trayecto indoor — potencia recibida vs distancia
%    3. Espectro de la interferencia WLAN sobre canales Bluetooth
%    4. Balance de potencias en el receptor Bluetooth (SIR)
% =========================================================================

clear; clc; close all;

%% -------------------------------------------------------------------------
%  SECCIÓN 1: PARÁMETROS DEL ESCENARIO
%  Todos los valores usados en bt_ber_comparison.m y main_simulation.m
% -------------------------------------------------------------------------

% --- Enlace Bluetooth BR/EDR ---
P_BT_tx_dBm   = 0;          % dBm — Clase 2, configurado en bluetoothConnectionConfig
f_BT_GHz      = 2.402;      % GHz — frecuencia del canal 0 (f_k = 2402 + k MHz)
d_BT_m        = 5.0;        % m   — distancia Central→Periférico ([0,0,0] a [5,0,0])

% --- Nodos WLAN interfirientes ---
P_WLAN_tx_dBm = 20;         % dBm — AP doméstico típico 802.11g
f_WLAN1_GHz   = 2.442;      % GHz — canal WLAN 7 (2437 + 5 MHz)
f_WLAN2_GHz   = 2.447;      % GHz — canal WLAN 8
BW_WLAN_MHz   = 20;         % MHz — ancho de banda 802.11g/n estándar

% Posiciones en metros [x, y, z]
pos_central    = [0, 0, 0];
pos_peripheral = [5, 0, 0];
pos_WLAN1      = [0, 7, 5];
pos_WLAN2      = [0, 3, 0];

% Distancias de cada nodo WLAN al receptor Bluetooth (periférico)
d_WLAN1_m = norm(pos_WLAN1 - pos_peripheral);  % m
d_WLAN2_m = norm(pos_WLAN2 - pos_peripheral);  % m

% --- Modelo de canal indoor (IEEE 802.15.2-2003, Sección 5) ---
n_indoor  = 3.0;    % exponente de pérdida indoor (n=2 free space, n=3-4 indoor)
d0_m      = 1.0;    % m — distancia de referencia

% Pérdida de trayecto en espacio libre a d0=1m, f=2.4 GHz
% PL_0 = 20*log10(4*pi*f*d0/c)
c_m_s  = 3e8;
PL0_dB = 20 * log10(4 * pi * 2.4e9 * d0_m / c_m_s);  % ≈ 40.1 dB

% --- Ruido térmico ---
kT_dBm_Hz = -174;      % dBm/Hz a temperatura ambiente (290 K)
BW_BT_Hz  = 1e6;       % Hz — ancho de banda de un canal Bluetooth BR
N_dBm     = kT_dBm_Hz + 10 * log10(BW_BT_Hz);  % piso de ruido = -114 dBm

%% -------------------------------------------------------------------------
%  SECCIÓN 2: CÁLCULO DEL BALANCE DE POTENCIAS
% -------------------------------------------------------------------------

% Función de pérdida de trayecto (log-distance model)
pathLoss = @(d) PL0_dB + 10 * n_indoor * log10(d / d0_m);

% Potencia recibida del enlace Bluetooth útil (en el periférico)
PL_BT_dB    = pathLoss(d_BT_m);
P_BT_rx_dBm = P_BT_tx_dBm - PL_BT_dB;

% Potencia recibida de WLAN 1 (en el periférico)
PL_W1_dB    = pathLoss(d_WLAN1_m);
P_W1_rx_dBm = P_WLAN_tx_dBm - PL_W1_dB;

% Potencia recibida de WLAN 2 (en el periférico)
PL_W2_dB    = pathLoss(d_WLAN2_m);
P_W2_rx_dBm = P_WLAN_tx_dBm - PL_W2_dB;

% SIR respecto a cada nodo WLAN (en canales afectados)
SIR_W1_dB = P_BT_rx_dBm - P_W1_rx_dBm;
SIR_W2_dB = P_BT_rx_dBm - P_W2_rx_dBm;
SIR_total_dB = P_BT_rx_dBm - ...
    10 * log10(10^(P_W1_rx_dBm/10) + 10^(P_W2_rx_dBm/10));

% SNR sin interferencia
SNR_dB = P_BT_rx_dBm - N_dBm;

% Imprimir resumen
fprintf('=== BALANCE DE POTENCIAS EN EL RECEPTOR BLUETOOTH ===\n');
fprintf('Modelo: Log-distance indoor (IEEE 802.15.2, n=%.1f, d0=%.0f m)\n\n', ...
        n_indoor, d0_m);
fprintf('%-35s %8.2f dBm\n', 'Potencia TX Bluetooth (Clase 2):', P_BT_tx_dBm);
fprintf('%-35s %8.2f dB\n',  'Pérdida de trayecto BT (5 m):',   PL_BT_dB);
fprintf('%-35s %8.2f dBm\n', 'Potencia RX Bluetooth útil:',      P_BT_rx_dBm);
fprintf('\n');
fprintf('%-35s %8.2f dBm\n', 'Potencia TX WLAN (AP típico):', P_WLAN_tx_dBm);
fprintf('%-35s %8.2f dB\n',  sprintf('PL WLAN1 (%.1f m):', d_WLAN1_m), PL_W1_dB);
fprintf('%-35s %8.2f dBm\n', 'Potencia RX interferencia WLAN1:', P_W1_rx_dBm);
fprintf('%-35s %8.2f dB\n',  sprintf('PL WLAN2 (%.1f m):', d_WLAN2_m), PL_W2_dB);
fprintf('%-35s %8.2f dBm\n', 'Potencia RX interferencia WLAN2:', P_W2_rx_dBm);
fprintf('\n');
fprintf('%-35s %8.2f dBm\n', 'Piso de ruido térmico (1 MHz):',  N_dBm);
fprintf('%-35s %8.2f dB\n',  'SNR (sin interferencia):',         SNR_dB);
fprintf('%-35s %8.2f dB\n',  'SIR vs WLAN1:',                   SIR_W1_dB);
fprintf('%-35s %8.2f dB\n',  'SIR vs WLAN2:',                   SIR_W2_dB);
fprintf('%-35s %8.2f dB\n',  'SIR total (ambos WLAN):',         SIR_total_dB);
fprintf('\nSIR negativo indica que la interferencia supera a la señal útil\n');
fprintf('→ explica el PER de 30%% sin AFH en los canales afectados.\n\n');

% Mínimo Eb/N0 teórico para GFSK BR con BER ≤ 0.1%
% Referencia: Proakis, Digital Communications, GFSK con BT=0.5
EbN0_min_dB = 10;       % dB — valor teórico conocido para GFSK BER=0.1%
EbN0_op_dB  = SNR_dB;   % 53 dB — nuestro SNR operacional
margen_dB   = EbN0_op_dB - EbN0_min_dB;  % ~43 dB de margen

fprintf('Margen de desempeño: %.1f dB sobre el SNR mínimo para BER = 0.1%%\n', margen_dB);

%% -------------------------------------------------------------------------
%  FIGURA 1: PLAN DE FRECUENCIAS 2.4 GHz
%  Muestra el solapamiento entre canales Bluetooth y WLAN
% -------------------------------------------------------------------------

figure('Name','Canal — Plan de Frecuencias 2.4 GHz','Position',[50 50 1200 450]);

% Frecuencias de los 79 canales Bluetooth (MHz)
bt_ch   = 0:78;
bt_freq = 2402 + bt_ch;   % MHz

% Clasificar canales: afectados por WLAN1, WLAN2, ambos, o libres
% WLAN de 20 MHz: ocupa desde fc-10 hasta fc+10 MHz
% WLAN1: 2442 ± 10 → 2432-2452 MHz → canales BT 30-50
% WLAN2: 2447 ± 10 → 2437-2457 MHz → canales BT 35-55
w1_min = f_WLAN1_GHz * 1e3 - BW_WLAN_MHz/2;   % MHz
w1_max = f_WLAN1_GHz * 1e3 + BW_WLAN_MHz/2;
w2_min = f_WLAN2_GHz * 1e3 - BW_WLAN_MHz/2;
w2_max = f_WLAN2_GHz * 1e3 + BW_WLAN_MHz/2;

affected_w1   = bt_freq >= w1_min & bt_freq <= w1_max;
affected_w2   = bt_freq >= w2_min & bt_freq <= w2_max;
affected_both = affected_w1 & affected_w2;
affected_any  = affected_w1 | affected_w2;
free          = ~affected_any;

hold on;

% Canales WLAN como rectángulos semitransparentes
patch([w1_min w1_max w1_max w1_min], [0 0 1 1], [0.9 0.3 0.3], ...
      'FaceAlpha', 0.25, 'EdgeColor', 'none', 'DisplayName', ...
      sprintf('WLAN canal 7 (%.3f GHz, 20 MHz)', f_WLAN1_GHz));
patch([w2_min w2_max w2_max w2_min], [0 0 1 1], [0.3 0.3 0.9], ...
      'FaceAlpha', 0.25, 'EdgeColor', 'none', 'DisplayName', ...
      sprintf('WLAN canal 8 (%.3f GHz, 20 MHz)', f_WLAN2_GHz));

% Canales Bluetooth como barras verticales
bar(bt_freq(free),          ones(1,sum(free)),          0.6, ...
    'FaceColor', [0.2 0.7 0.2], 'EdgeColor', 'none', 'DisplayName', ...
    sprintf('BT libre (%d canales)', sum(free)));
bar(bt_freq(affected_any & ~affected_both), ...
    ones(1,sum(affected_any & ~affected_both)), 0.6, ...
    'FaceColor', [0.9 0.5 0.1], 'EdgeColor', 'none', 'DisplayName', ...
    sprintf('BT interferido 1 WLAN (%d canales)', sum(affected_any & ~affected_both)));
bar(bt_freq(affected_both), ones(1,sum(affected_both)), 0.6, ...
    'FaceColor', [0.85 0.1 0.1], 'EdgeColor', 'none', 'DisplayName', ...
    sprintf('BT interferido 2 WLAN (%d canales)', sum(affected_both)));

% Frecuencias centrales WLAN
xline(f_WLAN1_GHz*1e3, 'r--', 'LineWidth', 1.2, 'HandleVisibility', 'off');
xline(f_WLAN2_GHz*1e3, 'b--', 'LineWidth', 1.2, 'HandleVisibility', 'off');

% Anotaciones
n_affected = sum(affected_any);
text(2443, 0.85, sprintf('%d canales BT\nafectados', n_affected), ...
     'FontSize', 9, 'HorizontalAlignment', 'center', 'FontWeight', 'bold');

xlabel('Frecuencia (MHz)', 'FontSize', 11);
title({'Plan de Frecuencias 2.4 GHz — Coexistencia Bluetooth BR/EDR y WLAN', ...
       'IEEE 802.15.2-2003: escenario de interferencia co-canal'}, ...
      'FontSize', 12, 'FontWeight', 'bold');
legend('Location', 'northwest', 'FontSize', 9);
xlim([2400 2485]); ylim([0 1.3]);
set(gca, 'YTick', [], 'FontSize', 10);
grid on; hold off;

%% -------------------------------------------------------------------------
%  FIGURA 2: PÉRDIDA DE TRAYECTO INDOOR
%  Modelo log-distance (IEEE 802.15.2, n=3) — potencia recibida vs distancia
% -------------------------------------------------------------------------

figure('Name','Canal — Pérdida de Trayecto Indoor','Position',[100 100 950 500]);

d_range = linspace(0.5, 20, 500);   % metros
PL_range = PL0_dB + 10 * n_indoor * log10(d_range / d0_m);

P_BT_range   = P_BT_tx_dBm   - PL_range;
P_WLAN_range = P_WLAN_tx_dBm - PL_range;

hold on;

% Curvas de potencia recibida
h1 = plot(d_range, P_BT_range,   'Color', [0.2 0.6 0.2], 'LineWidth', 2.0, ...
          'DisplayName', sprintf('Bluetooth TX = %d dBm (Clase 2)', P_BT_tx_dBm));
h2 = plot(d_range, P_WLAN_range, 'Color', [0.8 0.2 0.2], 'LineWidth', 2.0, ...
          'DisplayName', sprintf('WLAN TX = %d dBm (AP típico)', P_WLAN_tx_dBm));

% Piso de ruido
yline(N_dBm, 'k:', 'LineWidth', 1.2, 'HandleVisibility', 'off');
text(19, N_dBm + 1.5, sprintf('Ruido térmico: %.0f dBm', N_dBm), ...
     'FontSize', 8, 'HorizontalAlignment', 'right');

% Puntos de operación del escenario simulado
plot(d_BT_m,    P_BT_rx_dBm, 'go', 'MarkerSize', 10, 'MarkerFaceColor', 'g', ...
     'HandleVisibility', 'off');
plot(d_WLAN1_m, P_W1_rx_dBm, 'r^', 'MarkerSize', 9,  'MarkerFaceColor', 'r', ...
     'HandleVisibility', 'off');
plot(d_WLAN2_m, P_W2_rx_dBm, 'rs', 'MarkerSize', 9,  'MarkerFaceColor', 'r', ...
     'HandleVisibility', 'off');

% Anotaciones de los puntos de operación
text(d_BT_m + 0.3,    P_BT_rx_dBm + 1.5, ...
     sprintf('BT: %.1f dBm\n@ %.0f m', P_BT_rx_dBm, d_BT_m), ...
     'FontSize', 8, 'Color', [0.1 0.5 0.1]);
text(d_WLAN2_m + 0.3, P_W2_rx_dBm + 1.5, ...
     sprintf('WLAN2: %.1f dBm\n@ %.0f m', P_W2_rx_dBm, d_WLAN2_m), ...
     'FontSize', 8, 'Color', [0.7 0.1 0.1]);
text(d_WLAN1_m + 0.3, P_W1_rx_dBm + 1.5, ...
     sprintf('WLAN1: %.1f dBm\n@ %.1f m', P_W1_rx_dBm, d_WLAN1_m), ...
     'FontSize', 8, 'Color', [0.7 0.1 0.1]);

% Flecha indicando SIR negativo en el receptor
annotation('doublearrow', ...
    'X', [d_BT_m/20 d_BT_m/20], ...
    'Y', [(P_BT_rx_dBm+110)/100 (P_W2_rx_dBm+110)/100], ...
    'Color', 'k');

xlabel('Distancia (m)', 'FontSize', 11);
ylabel('Potencia recibida (dBm)', 'FontSize', 11);
title({'Modelo de Pérdida de Trayecto Indoor — Log-Distance', ...
       sprintf('PL(d) = %.1f + 10×%.1f×log₁₀(d/1m)  [IEEE 802.15.2-2003, n=%.1f]', ...
               PL0_dB, n_indoor, n_indoor)}, 'FontSize', 11, 'FontWeight', 'bold');
legend([h1 h2], 'Location', 'northeast', 'FontSize', 10);
xlim([0.5 20]); grid on; hold off;

%% -------------------------------------------------------------------------
%  FIGURA 3: ESPECTRO DE LA INTERFERENCIA WLAN SOBRE CANALES BLUETOOTH
% -------------------------------------------------------------------------

% Localizar archivo .bb
bbFileName = 'WLANHESUBandwidth20.bb';
if isfile(fullfile(pwd, bbFileName))
    bbFilePath = fullfile(pwd, bbFileName);
elseif ~isempty(which(bbFileName))
    bbFilePath = which(bbFileName);
else
    results = dir(fullfile(matlabroot, '**', bbFileName));
    if isempty(results)
        warning('Archivo %s no encontrado. Figura 3 omitida.', bbFileName);
        bbFilePath = '';
    else
        bbFilePath = fullfile(results(1).folder, results(1).name);
    end
end

if ~isempty(bbFilePath)
    % Leer señal WLAN del archivo .bb
    bbReader = comm.BasebandFileReader('Filename', bbFilePath);
    bbInfo   = info(bbReader);
    bbReader.SamplesPerFrame = bbInfo.NumSamplesInData;
    wlanSig  = bbReader();
    sr_wlan  = bbReader.SampleRate;

    figure('Name','Canal — Espectro WLAN sobre Canales BT','Position',[150 80 1200 480]);
    hold on;

    % PSD de la señal WLAN (centrada en f_WLAN2 como dominante)
    fftLen  = 2048;
    nSig    = length(wlanSig);
    winLen  = min(fftLen, nSig);
    [pxx, f_wlan] = pwelch(wlanSig, hann(winLen), floor(winLen/2), ...
                           fftLen, sr_wlan, 'centered');
    pxx_dB = 10 * log10(pxx / max(pxx));

    % Graficar PSD centrada en la frecuencia del nodo WLAN dominante (WLAN2)
    f_plot = (f_wlan / 1e6) + f_WLAN2_GHz * 1e3;   % MHz absolutos
    h_wlan = plot(f_plot, pxx_dB - max(pxx_dB), ...
                  'Color', [0.8 0.2 0.2], 'LineWidth', 1.8, ...
                  'DisplayName', sprintf('WLAN2 (%.3f GHz, 20 MHz)', f_WLAN2_GHz));

    % Canales Bluetooth libres (verde) y afectados (rojo)
    for k = 1:79
        fc = bt_freq(k);
        if affected_any(k)
            col = [0.9 0.3 0.3];
        else
            col = [0.3 0.7 0.3];
        end
        patch([fc-0.5 fc+0.5 fc+0.5 fc-0.5], [-52 -52 -48 -48], col, ...
              'FaceAlpha', 0.7, 'EdgeColor', 'none', 'HandleVisibility', 'off');
    end

    % Leyenda manual para los canales
    patch(NaN, NaN, [0.3 0.7 0.3], 'DisplayName', 'Canal BT libre');
    patch(NaN, NaN, [0.9 0.3 0.3], 'DisplayName', 'Canal BT interferido');

    xlabel('Frecuencia (MHz)', 'FontSize', 11);
    ylabel('PSD normalizada (dB)', 'FontSize', 11);
    title({'Espectro de Interferencia WLAN (20 MHz) sobre Canales Bluetooth BR/EDR', ...
           'Los canales BT bajo la PSD WLAN sufren interferencia co-canal'}, ...
          'FontSize', 11, 'FontWeight', 'bold');
    legend('Location', 'northwest', 'FontSize', 9);
    xlim([2400 2485]); ylim([-55 5]);
    grid on; hold off;
end

%% -------------------------------------------------------------------------
%  FIGURA 4: BALANCE DE POTENCIAS EN EL RECEPTOR BLUETOOTH
% -------------------------------------------------------------------------

figure('Name','Canal — Balance de Potencias (SIR)','Position',[200 120 800 500]);

categorias = {'Señal BT útil', 'Interferencia WLAN1', ...
              'Interferencia WLAN2', 'Ruido térmico'};
potencias  = [P_BT_rx_dBm, P_W1_rx_dBm, P_W2_rx_dBm, N_dBm];
colores_bar = [0.2 0.7 0.2;   % verde — señal útil
               0.9 0.5 0.1;   % naranja — WLAN1
               0.8 0.2 0.2;   % rojo — WLAN2
               0.5 0.5 0.5];  % gris — ruido

hold on;
for k = 1:4
    bar(k, potencias(k), 0.55, 'FaceColor', colores_bar(k,:), ...
        'EdgeColor', 'none');
    text(k, potencias(k) + 0.8, sprintf('%.1f dBm', potencias(k)), ...
         'HorizontalAlignment', 'center', 'FontWeight', 'bold', 'FontSize', 10);
end

% Línea de señal BT para visualizar SIR
yline(P_BT_rx_dBm, 'g--', 'LineWidth', 1.2, 'HandleVisibility', 'off');

% Anotaciones SIR
text(2.5, (P_BT_rx_dBm + P_W1_rx_dBm)/2, ...
     sprintf('SIR vs WLAN1\n= %.1f dB', SIR_W1_dB), ...
     'FontSize', 9, 'HorizontalAlignment', 'center', 'Color', [0.6 0.3 0]);
text(3.5, (P_BT_rx_dBm + P_W2_rx_dBm)/2, ...
     sprintf('SIR vs WLAN2\n= %.1f dB', SIR_W2_dB), ...
     'FontSize', 9, 'HorizontalAlignment', 'center', 'Color', [0.7 0.1 0.1]);

set(gca, 'XTick', 1:4, 'XTickLabel', categorias, 'FontSize', 10);
ylabel('Potencia en el receptor Bluetooth (dBm)', 'FontSize', 11);
title({'Balance de Potencias en el Receptor Bluetooth (nodo periférico)', ...
       sprintf('SIR total = %.1f dB → explica PER ≈ 30%% sin AFH', SIR_total_dB)}, ...
      'FontSize', 11, 'FontWeight', 'bold');
grid on; hold off;

fprintf('Script bt_channel completado. Figuras 1-4 generadas.\n');