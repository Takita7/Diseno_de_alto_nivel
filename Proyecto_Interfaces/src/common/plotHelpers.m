function ph = plotHelpers()
%plotHelpers Shared figure-styling functions for Tx/Channel/Rx figures
%   PH = plotHelpers() returns a struct of function handles so the video's
%   simulation-evidence figures read as one coherent artifact rather than
%   three mismatched outputs (plan.md UX Consistency row, Constitution III
%   adapted for a non-interactive simulation feature). Usage:
%
%     ph = plotHelpers();
%     fig = ph.perComparisonFigure(report.per.afhOff, report.per.afhOn);
%     ph.saveFigure(fig, 'results/figures', 'per_afh_comparison');

ph.applyStyle = @applyStyle;
ph.perComparisonFigure = @perComparisonFigure;
ph.saveFigure = @saveFigure;

end

function applyStyle(ax, titleStr, xlabelStr, ylabelStr)
%applyStyle Apply the project's consistent axes styling
title(ax, titleStr, 'FontWeight', 'bold', 'FontSize', 12);
xlabel(ax, xlabelStr, 'FontWeight', 'bold');
ylabel(ax, ylabelStr, 'FontWeight', 'bold');
box(ax, 'on');
grid(ax, 'on');
ax.FontSize = 10;
end

function fig = perComparisonFigure(perOff, perOn)
%perComparisonFigure Bar chart comparing PER with AFH disabled vs. enabled
%   Colors are fixed project-wide: AFH-disabled is amber (the "before"/
%   unmitigated condition), AFH-enabled is blue (the "after"/mitigated
%   condition), matching every other figure this project produces.
COLOR_AFH_OFF = [0.85, 0.33, 0.10];
COLOR_AFH_ON = [0.00, 0.45, 0.74];

fig = figure('Visible', 'off');
ax = axes(fig);
b = bar(ax, categorical({'AFH disabled', 'AFH enabled'}), [perOff, perOn] * 100);
b.FaceColor = 'flat';
b.CData(1, :) = COLOR_AFH_OFF;
b.CData(2, :) = COLOR_AFH_ON;
applyStyle(ax, 'Packet Error Rate: AFH Disabled vs. Enabled', 'Condition', 'PER (%)');
end

function saveFigure(fig, outDir, name)
%saveFigure Save FIG as both .png and .fig under OUTDIR/NAME
if ~isfolder(outDir)
    mkdir(outDir);
end
saveas(fig, fullfile(outDir, [char(name) '.png']));
saveas(fig, fullfile(outDir, [char(name) '.fig']));
end
