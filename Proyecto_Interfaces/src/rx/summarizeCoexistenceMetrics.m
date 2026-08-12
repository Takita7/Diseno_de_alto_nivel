function report = summarizeCoexistenceMetrics(runOff, runOn)
%summarizeCoexistenceMetrics Compare AFH-off vs AFH-on runs (Contract 2)
%   REPORT = summarizeCoexistenceMetrics(RUNOFF, RUNON) accepts two
%   runCoexistenceScenario.m outputs — one with afhEnabled=false, one with
%   afhEnabled=true, same scenario, in either argument order — and returns a
%   PerformanceMetricsReport struct (data-model.md) with per.afhOff,
%   per.afhOn, and afhImprovementDelta populated. Satisfies FR-007/US3
%   Acceptance Scenario 2.
%
%   ber, performanceMargin, spectralEfficiencyBitsPerHz, and
%   degradationSensitivity are left as placeholders (NaN / "") here — fill
%   them from measureLinkBER, computePerformanceMargin, and
%   computeSpectralEfficiency per quickstart.md Step 6.
%
%   Per Contract 2's failure mode: rejects mismatched scenario values
%   between the two runs (comparing PER across two different scenarios is
%   not a valid AFH-mitigation comparison, per FR-007's "under the same
%   interference scenario" requirement), and rejects an empty perPacketLog
%   on either run rather than silently reporting per = 0.

[runOff, runOn] = orderByAfhFlag(runOff, runOn);

if ~isequal(runOff.scenario, runOn.scenario)
    error("summarizeCoexistenceMetrics:MismatchedScenario", ...
        "runOff.scenario and runOn.scenario must be identical (FR-007 requires comparing the same interference scenario).");
end
if isempty(runOff.perPacketLog)
    error("summarizeCoexistenceMetrics:EmptyLog", "runOff.perPacketLog must be non-empty.");
end
if isempty(runOn.perPacketLog)
    error("summarizeCoexistenceMetrics:EmptyLog", "runOn.perPacketLog must be non-empty.");
end

perOff = 1 - mean(runOff.perPacketLog.SuccessStatus);
perOn = 1 - mean(runOn.perPacketLog.SuccessStatus);

report = struct( ...
    'scenario', runOn.scenario, ...
    'per', struct('afhOff', perOff, 'afhOn', perOn), ...
    'ber', NaN, ...
    'performanceMargin', NaN, ...
    'spectralEfficiencyBitsPerHz', NaN, ...
    'degradationSensitivity', "", ...
    'afhImprovementDelta', perOff - perOn);

end

function [runOff, runOn] = orderByAfhFlag(a, b)
%orderByAfhFlag Return the two runs ordered (afhEnabled=false, afhEnabled=true)
if ~isfield(a, 'afhEnabled') || ~isfield(b, 'afhEnabled')
    error("summarizeCoexistenceMetrics:MissingAfhEnabled", ...
        "Both runs must have an afhEnabled field.");
end
if isequal(logical(a.afhEnabled), logical(b.afhEnabled))
    error("summarizeCoexistenceMetrics:SameAfhCondition", ...
        "The two runs must differ in afhEnabled (one false, one true) to be compared.");
end
if ~a.afhEnabled
    runOff = a;
    runOn = b;
else
    runOff = b;
    runOn = a;
end
end
