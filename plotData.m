clc; clear all; close all;
file = 'multiTest_chan';
channels = 8;

dataLength = 10e3;
offset = 10e6;

data=[];
for i=1:channels
    filename = [file num2str(i-1) '.bin'];
    data = [data readData(filename, dataLength, offset)];
end

% Read single column
% dataFFT = fftshift(fft(data(:,1,1:end)));
% Read all the data
dataFFT = fftshift(fft(data(1:end,1:end)));

figure()
plot(10*log(abs(dataFFT)))