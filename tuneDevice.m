clc; clear all; close all;
filename = 'tune_chan0.bin';

data = readTune(filename);

dataFFT = fftshift(fft(data(1:end,1:end)));

stepSize = 2;
minTune = 50;
maxTune = 90;
skip = 1;

numSteps = ceil((maxTune-minTune+1)/stepSize);
numSamples = length(data);

steps = 0:numSamples/numSteps:numSamples-(numSamples/numSteps);
gain = minTune:stepSize:maxTune;
L = strsplit(sprintf('%.1f\n', gain(:)), '\n');

figure()
subplot(311)
yyaxis left
plot(abs(data))
hold on
grid on
yyaxis right
plot(steps,gain,'.')
text(steps(1:skip:end),gain(1:skip:end),L(1:skip:length(steps)),'HorizontalAlignment','center', 'VerticalAlignment','bottom')
title('Linear')

subplot(312)
yyaxis left
plot(abs(10*log(data)))
hold on
grid on
yyaxis right
plot(steps,gain,'.')
text(steps(1:skip:end),gain(1:skip:end),L(1:skip:length(steps)),'HorizontalAlignment','center', 'VerticalAlignment','bottom')
title('Logarithmic')

subplot(313)
yyaxis left
plot(real(data))
hold on
grid on
yyaxis right
plot(steps,gain,'.')
text(steps(1:skip:end),gain(1:skip:end),L(1:skip:length(steps)),'HorizontalAlignment','center', 'VerticalAlignment','bottom')
title('Real')