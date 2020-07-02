clc; clear all;
fprintf('Reading BIN file..\n')
filename = 'samp.bin_chan2.bin';
dataLength = (10e6)*1;
offset = 0;
fid1 = fopen(filename, 'r', 'ieee-le');
fseek(fid1, offset, 'bof'); % fileID, Offset in Bytes, Start point
rawData = fread(fid1, dataLength, 'float', 0, 'ieee-le');

if mod(length(rawData),2) == 0
    data = rawData(1:2:end) + 1i*rawData(2:2:end);
else
    data = rawData(1:2:end-1) + 1i*rawData(2:2:end);
end

dataFFT = fftshift(fft(data));

figure()
plot(10*log(abs(dataFFT)))