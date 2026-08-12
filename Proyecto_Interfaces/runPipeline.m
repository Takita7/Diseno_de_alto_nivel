%% runPipeline Run the full Bluetooth link simulation pipeline and persist evidence
% Executes quickstart.md Steps 1-6 (User Stories 1-3) end to end and saves the
% resulting figures/metrics under results/, for the video's simulation-
% evidence segment (FR-008/FR-009). This is what T049 wires up: previously
% nothing produced above lived on disk, only in the console / a live figure.

startup;
rng(1, "twister");

projectRoot = fileparts(mfilename('fullpath'));
figuresDir = fullfile(projectRoot, 'results', 'figures');
metricsDir = fullfile(projectRoot, 'results', 'metrics');

fprintf('--- User Story 1: BR/EDR + LE waveform conformance ---\n');
wfmBREDR = generateBREDRWaveform();
berBREDR = measureLinkBER(wfmBREDR);
fprintf('BR/EDR link-level BER = %g (valid=%d)\n', berBREDR.ber, berBREDR.pktValidStatus);

wfmLE = generateLEWaveform();
berLE = measureLinkBER(wfmLE);
fprintf('LE link-level BER = %g (valid=%d)\n', berLE.ber, berLE.pktValidStatus);

fprintf('\n--- User Stories 2-3: coexistence scenario, AFH disabled vs. enabled ---\n');
runOff = runCoexistenceScenario('AFHEnabled', false);
runOn = runCoexistenceScenario('AFHEnabled', true);
report = summarizeCoexistenceMetrics(runOff, runOn);

report.ber = berBREDR.ber;
report.spectralEfficiencyBitsPerHz = computeSpectralEfficiency(runOn);
report.performanceMargin = computePerformanceMargin(runOn);
report.degradationSensitivity = strjoin([ ...
    "As the WLAN interference footprint grows (more overlapping channels or", ...
    "a higher interferer duty cycle), PER is expected to rise further and", ...
    "the AFH benefit to shrink, since fewer channels remain classifiable as", ...
    "good. Qualitative only, per FR-004 (no sensitivity sweep in this", ...
    "feature's bounded scope)."]);

fprintf('PER: off=%.4f on=%.4f delta=%.4f\n', report.per.afhOff, report.per.afhOn, report.afhImprovementDelta);
fprintf('BER=%.4g  spectral efficiency=%.4f bits/s/Hz  performance margin=%.1f dB\n', ...
    report.ber, report.spectralEfficiencyBitsPerHz, report.performanceMargin);

fprintf('\n--- Persisting figures and metrics (results/) ---\n');
ph = plotHelpers();
figPER = ph.perComparisonFigure(report.per.afhOff, report.per.afhOn);
ph.saveFigure(figPER, figuresDir, 'per_afh_comparison');
close(figPER);

if ~isfolder(metricsDir)
    mkdir(metricsDir);
end
timestamp = string(datetime('now'), 'yyyyMMdd_HHmmss');
save(fullfile(metricsDir, "performance_metrics_report_" + timestamp + ".mat"), 'report');

metricsTable = table(report.per.afhOff, report.per.afhOn, report.afhImprovementDelta, ...
    report.ber, report.performanceMargin, report.spectralEfficiencyBitsPerHz, ...
    'VariableNames', {'PER_AFHOff', 'PER_AFHOn', 'AFHImprovementDelta', 'BER', ...
    'PerformanceMarginDB', 'SpectralEfficiencyBitsPerHz'});
writetable(metricsTable, fullfile(metricsDir, "performance_metrics_report_" + timestamp + ".csv"));

fprintf('Saved figures to %s\n', figuresDir);
fprintf('Saved metrics to %s\n', metricsDir);
