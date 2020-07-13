function data = readTune(filename)
    fprintf('Reading BIN file..\n')
    fid = fopen(filename, 'r', 'ieee-le');
    rawData = fread(fid, 'short');

    if mod(length(rawData),2) == 0
        data = rawData(1:2:end) + 1i*rawData(2:2:end);
    else
        data = rawData(1:2:end-1) + 1i*rawData(2:2:end);
    end
    fprintf('Complete\n')
end