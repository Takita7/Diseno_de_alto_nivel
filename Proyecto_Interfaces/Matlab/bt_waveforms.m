%% =========================================================================
%  EQUIPO 5 — Generación de Formas de Onda PHY: Bluetooth BR/EDR y LE
%  Curso: MP-6159 Interfaces de Comunicaciones — ITCR
%
%  Modos generados:
%    BR/EDR → bluetoothWaveformGenerator (1 output en R2026a)
%    LE     → bleWaveformGenerator       (Name-Value arguments)
%
%  FIGURAS:
%    1. Dominio del tiempo — I/Q de los 5 modos
%    2. PSD comparativa — anchos de banda BR/EDR vs LE
%    3. Modulación — frecuencia instantánea (GFSK) y constelaciones (EDR)
%    4. Eficiencia espectral por modo
% =========================================================================

clear; clc; close all;
rng(42, 'twister');

samplesPerSymbol = 8;

%% -------------------------------------------------------------------------
%  SECCIÓN 1: CONFIGURACIÓN
% -------------------------------------------------------------------------

% --- BR/EDR (bluetoothWaveformConfig) ---
cfgBR = bluetoothWaveformConfig;
cfgBR.Mode             = 'BR';
cfgBR.PacketType       = 'DH1';
cfgBR.PayloadLength    = 27;
cfgBR.SamplesPerSymbol = samplesPerSymbol;

cfgEDR2 = bluetoothWaveformConfig;
cfgEDR2.Mode             = 'EDR2M';
cfgEDR2.PacketType       = '2-DH1';
cfgEDR2.PayloadLength    = 54;
cfgEDR2.SamplesPerSymbol = samplesPerSymbol;

cfgEDR3 = bluetoothWaveformConfig;
cfgEDR3.Mode             = 'EDR3M';
cfgEDR3.PacketType       = '3-DH1';
cfgEDR3.PayloadLength    = 83;
cfgEDR3.SamplesPerSymbol = samplesPerSymbol;

% Sample rates BR/EDR: 1 Msym/s × 8 = 8 MHz
srBR = 1e6 * samplesPerSymbol;

% --- LE (bleWaveformGenerator — sin objeto config, usa Name-Value) ---
% LE1M: GFSK 1 Mbps, canal 2 MHz, 40 canales (37 data + 3 advertising)
% LE2M: GFSK 2 Mbps, canal 2 MHz
% ModulationIndex=0.5 (diferencia clave respecto a BR que usa 0.32)
% ChannelIndex=37 → canal de advertising (2.402 GHz)
leMsg = randi([0 1], 160, 1);   % 20 bytes de payload LE

srLE1M = 1e6 * samplesPerSymbol;   %  8 MHz (1 Msym/s)
srLE2M = 2e6 * samplesPerSymbol;   % 16 MHz (2 Msym/s)

fprintf('=== CONFIGURACIÓN DE MODOS PHY ===\n');
fprintf('%-8s %-15s %-10s %-10s %-12s\n', ...
        'Modo','Modulación','BitRate','BW canal','ModIndex');
fprintf('%s\n', repmat('-',1,60));
fprintf('%-8s %-15s %-10s %-10s %-12.2f\n','BR',    'GFSK',       '1 Mbps','1 MHz', 0.32);
fprintf('%-8s %-15s %-10s %-10s %-12s\n',  'EDR2M', 'pi/4-DQPSK', '2 Mbps','1 MHz','N/A');
fprintf('%-8s %-15s %-10s %-10s %-12s\n',  'EDR3M', '8DPSK',      '3 Mbps','1 MHz','N/A');
fprintf('%-8s %-15s %-10s %-10s %-12.2f\n','LE1M',  'GFSK',       '1 Mbps','2 MHz', 0.50);
fprintf('%-8s %-15s %-10s %-10s %-12.2f\n','LE2M',  'GFSK',       '2 Mbps','2 MHz', 0.50);
fprintf('\n');

%% -------------------------------------------------------------------------
%  SECCIÓN 2: GENERACIÓN DE WAVEFORMS
% -------------------------------------------------------------------------

fprintf('Generando waveforms...\n');

% BR/EDR — un solo output en R2026a
bitsBR   = randi([0 1], cfgBR.PayloadLength   * 8, 1);
bitsEDR2 = randi([0 1], cfgEDR2.PayloadLength * 8, 1);
bitsEDR3 = randi([0 1], cfgEDR3.PayloadLength * 8, 1);

wvBR   = bluetoothWaveformGenerator(bitsBR,   cfgBR);
wvEDR2 = bluetoothWaveformGenerator(bitsEDR2, cfgEDR2);
wvEDR3 = bluetoothWaveformGenerator(bitsEDR3, cfgEDR3);

% LE — bleWaveformGenerator con Name-Value
wvLE1M = bleWaveformGenerator(leMsg, ...
    Mode             = "LE1M", ...
    ChannelIndex     = 37, ...
    SamplesPerSymbol = samplesPerSymbol, ...
    ModulationIndex  = 0.5);

wvLE2M = bleWaveformGenerator(leMsg, ...
    Mode             = "LE2M", ...
    ChannelIndex     = 37, ...
    SamplesPerSymbol = samplesPerSymbol, ...
    ModulationIndex  = 0.5);

fprintf('Waveforms generados: BR(%d), EDR2M(%d), EDR3M(%d), LE1M(%d), LE2M(%d) muestras\n\n', ...
    length(wvBR), length(wvEDR2), length(wvEDR3), length(wvLE1M), length(wvLE2M));

% Ejes de tiempo (µs)
tBR   = (0:length(wvBR)   - 1) / srBR   * 1e6;
tEDR2 = (0:length(wvEDR2) - 1) / srBR   * 1e6;
tEDR3 = (0:length(wvEDR3) - 1) / srBR   * 1e6;
tLE1M = (0:length(wvLE1M) - 1) / srLE1M * 1e6;
tLE2M = (0:length(wvLE2M) - 1) / srLE2M * 1e6;

% Ventana de visualización temporal (primeros 60 µs)
nBR   = min(length(wvBR),   round(60e-6 * srBR));
nEDR2 = min(length(wvEDR2), round(60e-6 * srBR));
nEDR3 = min(length(wvEDR3), round(60e-6 * srBR));
nLE1M = min(length(wvLE1M), round(60e-6 * srLE1M));
nLE2M = min(length(wvLE2M), round(60e-6 * srLE2M));

colores = [0.1 0.5 0.1;    % verde  — BR
           0.2 0.2 0.8;    % azul   — EDR2M
           0.8 0.1 0.1;    % rojo   — EDR3M
           0.8 0.5 0.0;    % naranja— LE1M
           0.5 0.0 0.8];   % violeta— LE2M

%% -------------------------------------------------------------------------
%  FIGURA 1: DOMINIO DEL TIEMPO
% -------------------------------------------------------------------------

modos  = {'BR (GFSK, 1 Mbps)',     'EDR2M (pi/4-DQPSK, 2 Mbps)', ...
          'EDR3M (8DPSK, 3 Mbps)', 'LE1M (GFSK, 1 Mbps, h=0.5)', ...
          'LE2M (GFSK, 2 Mbps, h=0.5)'};
wvs    = {wvBR,   wvEDR2,  wvEDR3,  wvLE1M,  wvLE2M};
ts     = {tBR,    tEDR2,   tEDR3,   tLE1M,   tLE2M};
nShows = {nBR,    nEDR2,   nEDR3,   nLE1M,   nLE2M};

figure('Name','Bluetooth PHY — Dominio del Tiempo','Position',[50 30 1050 720]);
for k = 1:5
    subplot(5,1,k);
    wv = wvs{k}; t = ts{k}; n = nShows{k};
    plot(t(1:n), real(wv(1:n)), 'Color', colores(k,:),        'LineWidth', 1.1);
    hold on;
    plot(t(1:n), imag(wv(1:n)), 'Color', colores(k,:) * 0.55, ...
         'LineWidth', 0.8, 'LineStyle', '--');
    title(modos{k}, 'FontSize', 9, 'FontWeight', 'bold');
    ylabel('Amp.', 'FontSize', 8); grid on; ylim([-1.5 1.5]);
    if k == 1
        legend('I (real)','Q (imag)','Location','northeast','FontSize',7);
    end
    if k == 5; xlabel('Tiempo (µs)', 'FontSize', 9); end
end
sgtitle('Bluetooth BR/EDR y LE — Dominio del Tiempo (primeros 60 µs)', ...
        'FontSize', 12, 'FontWeight', 'bold');

%% -------------------------------------------------------------------------
%  FIGURA 2: PSD COMPARATIVA — BR/EDR (8 MHz) vs LE2M (16 MHz)
%  Nota: para comparar espectros correctamente cada modo se grafica
%  con su propio sample rate.
% -------------------------------------------------------------------------

srs = [srBR, srBR, srBR, srLE1M, srLE2M];
bwLimits = [0.5, 0.5, 0.5, 1.0, 1.0];   % MHz (mitad del canal por modo)

figure('Name','Bluetooth PHY - PSD por Modo','Position',[80 40 1150 750]);

for k = 1:5
    subplot(3, 2, k);
    nSig     = length(wvs{k});
    winLen   = min(512, nSig);
    noverlap = floor(winLen * 0.75);
    [pxx, f] = pwelch(wvs{k}, hann(winLen), noverlap, 1024, srs(k), 'centered');
    pxx_dB   = 10 * log10(pxx / max(pxx));
    plot(f/1e6, pxx_dB, 'Color', colores(k,:), 'LineWidth', 1.8);
    xline(-bwLimits(k), 'k--', 'LineWidth', 0.9);
    xline( bwLimits(k), 'k--', 'LineWidth', 0.9);
    yline(-3, 'Color', [0.5 0.5 0.5], 'LineStyle', ':', 'LineWidth', 1.0, ...
          'Label', '-3 dB', 'FontSize', 7, 'LabelHorizontalAlignment', 'left');
    bwLabel = sprintf('Canal: %.0f MHz', bwLimits(k)*2);
    text(0, -8, bwLabel, 'HorizontalAlignment', 'center', ...
         'FontSize', 8, 'Color', [0.3 0.3 0.3]);
    xlabel('Frecuencia relativa (MHz)', 'FontSize', 8);
    ylabel('PSD norm. (dB)',            'FontSize', 8);
    title(modos{k}, 'FontSize', 9, 'FontWeight', 'bold');
    xlim([-3 3]); ylim([-50 5]); grid on;
end

% Panel 6: overlay de todos los modos
subplot(3, 2, 6);
hold on; h_ov = zeros(1,5);
for k = 1:5
    nSig     = length(wvs{k});
    winLen   = min(512, nSig);
    noverlap = floor(winLen * 0.75);
    [pxx, f] = pwelch(wvs{k}, hann(winLen), noverlap, 1024, srs(k), 'centered');
    pxx_dB   = 10 * log10(pxx / max(pxx));
    h_ov(k)  = plot(f/1e6, pxx_dB, 'Color', colores(k,:), 'LineWidth', 1.4);
end
xline(-0.5,'k--','LineWidth',0.8); xline(0.5,'k--','LineWidth',0.8);
xline(-1.0,'k:', 'LineWidth',0.8); xline(1.0,'k:', 'LineWidth',0.8);
legend(h_ov, {'BR','EDR2M','EDR3M','LE1M','LE2M'}, ...
       'Location','northeast','FontSize',7);
xlabel('Frecuencia relativa (MHz)','FontSize',8);
ylabel('PSD norm. (dB)','FontSize',8);
title('Comparacion - todos los modos','FontSize',9,'FontWeight','bold');
xlim([-3 3]); ylim([-50 5]); grid on; hold off;

sgtitle('Bluetooth BR/EDR y LE - Densidad Espectral de Potencia', ...
        'FontSize', 12, 'FontWeight', 'bold');

%% -------------------------------------------------------------------------
%  FIGURA 3: FRECUENCIA INSTANTANEA GFSK (BR vs LE1M)
%  Muestra la diferencia de indice de modulacion entre BR (h=0.32) y LE (h=0.5)
% -------------------------------------------------------------------------

figure('Name','Bluetooth - GFSK: Frecuencia Instantanea','Position',[120 120 900 420]);

% Calcular frecuencia instantanea BR
nF   = min(length(wvBR),   round(40e-6 * srBR));
ph   = unwrap(angle(wvBR(1:nF)));
instF_BR = diff(ph) / (2*pi) * srBR / 1e3;   % kHz
tF_BR    = (1:length(instF_BR)) / srBR * 1e6; % us

% Calcular frecuencia instantanea LE1M
nF2  = min(length(wvLE1M), round(40e-6 * srLE1M));
ph2  = unwrap(angle(wvLE1M(1:nF2)));
instF_LE = diff(ph2) / (2*pi) * srLE1M / 1e3;
tF_LE    = (1:length(instF_LE)) / srLE1M * 1e6;

hBR = plot(tF_BR, instF_BR, 'Color', colores(1,:), 'LineWidth', 1.4);
hold on;
hLE = plot(tF_LE, instF_LE, 'Color', colores(4,:), 'LineWidth', 1.4);

% Lineas de referencia (HandleVisibility='off' para no aparecer en leyenda)
yline( 160, 'k--', 'LineWidth', 1.0, 'HandleVisibility', 'off');
yline(-160, 'k--', 'LineWidth', 1.0, 'HandleVisibility', 'off');
yline( 250, 'r--', 'LineWidth', 1.0, 'HandleVisibility', 'off');
yline(-250, 'r--', 'LineWidth', 1.0, 'HandleVisibility', 'off');

% Anotaciones manuales de las lineas de referencia
text(41,  175, '+160 kHz (BR, h=0.32)',  'FontSize', 8, 'Color', 'k', 'HorizontalAlignment', 'right');
text(41, -175, '-160 kHz (BR, h=0.32)',  'FontSize', 8, 'Color', 'k', 'HorizontalAlignment', 'right');
text(41,  265, '+250 kHz (LE1M, h=0.50)','FontSize', 8, 'Color', 'r', 'HorizontalAlignment', 'right');
text(41, -265, '-250 kHz (LE1M, h=0.50)','FontSize', 8, 'Color', 'r', 'HorizontalAlignment', 'right');

legend([hBR, hLE], {'BR (h=0.32, desv. +/-160 kHz)', 'LE1M (h=0.50, desv. +/-250 kHz)'}, ...
       'Location', 'south', 'FontSize', 9);
xlabel('Tiempo (us)', 'FontSize', 11);
ylabel('Frecuencia instantanea (kHz)', 'FontSize', 11);
title({'GFSK: Comparacion de Indice de Modulacion — BR vs LE1M', ...
       'Mismo tipo de modulacion, diferente desviacion de frecuencia'}, ...
      'FontSize', 11, 'FontWeight', 'bold');
ylim([-400 400]); xlim([0 40]); grid on; hold off;

%% -------------------------------------------------------------------------
%  FIGURA 4: CONSTELACIONES EDR (separada de la freq. instantanea)
% -------------------------------------------------------------------------

figure('Name','Bluetooth EDR - Constelaciones','Position',[170 80 900 430]);

% Panel izquierdo: EDR2M (pi/4-DQPSK — 4 estados de fase rotados)
subplot(1, 2, 1);
nP2   = round(0.60 * length(wvEDR2));
syms2 = wvEDR2(end - nP2 + 1 : samplesPerSymbol : end);
scatter(real(syms2), imag(syms2), 25, 'filled', ...
        'MarkerFaceColor', colores(2,:), 'MarkerFaceAlpha', 0.5);
hold on;
xlabel('I (componente en fase)', 'FontSize', 10);
ylabel('Q (componente en cuadratura)', 'FontSize', 10);
title({'EDR2M — pi/4-DQPSK', '4 grupos de fase (rotacion de pi/4 por simbolo)'}, ...
      'FontSize', 10, 'FontWeight', 'bold');
axis equal; xlim([-1.5 1.5]); ylim([-1.5 1.5]);
xline(0, 'k:', 'LineWidth', 0.8, 'HandleVisibility', 'off');
yline(0, 'k:', 'LineWidth', 0.8, 'HandleVisibility', 'off');
% Circulo unitario de referencia
theta = linspace(0, 2*pi, 200);
plot(cos(theta), sin(theta), 'k:', 'LineWidth', 0.7, 'HandleVisibility', 'off');
grid on;

% Panel derecho: EDR3M (8DPSK — 8 estados de fase)
subplot(1, 2, 2);
nP3   = round(0.60 * length(wvEDR3));
syms3 = wvEDR3(end - nP3 + 1 : samplesPerSymbol : end);
scatter(real(syms3), imag(syms3), 25, 'filled', ...
        'MarkerFaceColor', colores(3,:), 'MarkerFaceAlpha', 0.5);
hold on;
xlabel('I (componente en fase)', 'FontSize', 10);
ylabel('Q (componente en cuadratura)', 'FontSize', 10);
title({'EDR3M — 8DPSK', '8 estados de fase (separacion de pi/4 entre estados)'}, ...
      'FontSize', 10, 'FontWeight', 'bold');
axis equal; xlim([-1.5 1.5]); ylim([-1.5 1.5]);
xline(0, 'k:', 'LineWidth', 0.8, 'HandleVisibility', 'off');
yline(0, 'k:', 'LineWidth', 0.8, 'HandleVisibility', 'off');
plot(cos(theta), sin(theta), 'k:', 'LineWidth', 0.7, 'HandleVisibility', 'off');
grid on;

sgtitle('Bluetooth EDR — Diagramas de Constelacion', ...
        'FontSize', 12, 'FontWeight', 'bold');

%% -------------------------------------------------------------------------
%  FIGURA 4: EFICIENCIA ESPECTRAL
% -------------------------------------------------------------------------

figure('Name','Eficiencia Espectral','Position',[200 200 800 420]);

labels_ef  = {'BR','EDR2M','EDR3M','LE1M','LE2M'};
eficiencia = [1.0, 2.0, 3.0, 0.5, 1.0];

b = bar(eficiencia, 0.55, 'FaceColor', 'flat');
b.CData = colores;
hold on;
for k = 1:5
    text(k, eficiencia(k) + 0.06, sprintf('%.1f bps/Hz', eficiencia(k)), ...
         'HorizontalAlignment','center','FontWeight','bold','FontSize',10);
end
set(gca,'XTickLabel',labels_ef,'FontSize',10);
ylabel('Eficiencia espectral teórica (bps/Hz)','FontSize',11);
title({'Bluetooth — Eficiencia Espectral por Modo PHY', ...
       'Throughput bruto / Ancho de banda de canal'},'FontSize',11);
ylim([0 3.8]); grid on;
yline(1,'k--','1 bps/Hz (BR)','FontSize',8,'LabelHorizontalAlignment','left');

%% -------------------------------------------------------------------------
%  RESUMEN EN CONSOLA
% -------------------------------------------------------------------------

fprintf('=== RESUMEN DE PARÁMETROS PHY ===\n');
fprintf('%-8s %-10s %-10s %-10s %-10s %-12s\n',...
        'Modo','Modulación','SymRate','BitRate','BW canal','Ef.(bps/Hz)');
fprintf('%s\n', repmat('-',1,65));
datos = {'BR','GFSK','1 Msym/s','1 Mbps','1 MHz','1.0';
         'EDR2M','pi/4-DQPSK','1 Msym/s','2 Mbps','1 MHz','2.0';
         'EDR3M','8DPSK','1 Msym/s','3 Mbps','1 MHz','3.0';
         'LE1M','GFSK (h=0.5)','1 Msym/s','1 Mbps','2 MHz','0.5';
         'LE2M','GFSK (h=0.5)','2 Msym/s','2 Mbps','2 MHz','1.0'};
for k = 1:5
    fprintf('%-8s %-10s %-10s %-10s %-10s %-12s\n', datos{k,:});
end
fprintf('\nScript bt_waveforms completado.\n');