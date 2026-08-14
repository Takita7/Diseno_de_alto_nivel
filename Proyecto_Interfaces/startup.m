% startup Add this project's src/ tree (and the mandatory-example folder) to the MATLAB path.
%   Run once per session from this directory, or let MATLAB auto-run it by
%   launching MATLAB with this folder as the working directory.

thisDir = fileparts(mfilename('fullpath'));
addpath(genpath(fullfile(thisDir, 'src')));
addpath(fullfile(thisDir, 'Matlab')); % helperInterferingWLANNode, helperBluetoothChannelClassification, helperVisualizeCoexistence, WLANHESUBandwidth20.bb
