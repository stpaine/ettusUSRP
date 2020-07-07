function data = readData(filename, dataLength, offset)
    fprintf('Reading BIN file..\n')
    fid = fopen(filename, 'r', 'ieee-le');
    fseek(fid, offset, 'bof'); % fileID, Offset in Bytes, Start point

    rawData = fread(fid, dataLength, 'short', 0, 'ieee-le');

    if mod(length(rawData),2) == 0
        data = rawData(1:2:end) + 1i*rawData(2:2:end);
    else
        data = rawData(1:2:end-1) + 1i*rawData(2:2:end);
    end
    fprintf('Complete\n')
end