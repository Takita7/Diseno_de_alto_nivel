%% runTests Run the full matlab.unittest suite (tests/unit, tests/contract, tests/integration)
% Constitution II requires automated test coverage; this is the single
% entry point that runs all of it and fails loudly if anything regresses.

startup;
import matlab.unittest.TestSuite
import matlab.unittest.TestRunner

projectRoot = fileparts(mfilename('fullpath'));
suite = [TestSuite.fromFolder(fullfile(projectRoot, 'tests', 'unit')), ...
    TestSuite.fromFolder(fullfile(projectRoot, 'tests', 'contract')), ...
    TestSuite.fromFolder(fullfile(projectRoot, 'tests', 'integration'))];

runner = TestRunner.withTextOutput;
results = runner.run(suite);

disp(table(results));
fprintf('\nTotal: %d, Passed: %d, Failed: %d, Incomplete: %d\n', ...
    numel(results), nnz([results.Passed]), nnz([results.Failed]), nnz([results.Incomplete]));

if any([results.Failed])
    error('runTests:Failures', 'Test suite has failures.');
end
