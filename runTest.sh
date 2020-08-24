#!/bin/bash

# addr0=192.168.40.2
# addr1=192.168.50.2
# 2TB NVME /mnt/speedy
# 4TB SSD /mnt/fatty

#./usrpMultiSample --dev="addr0=192.168.40.2" --file="/mnt/speedy/refBuild" --duration="20" --rate="10e6" --freq="514.1666e6" --gain="80" --wait="0" --chan="1" --print="n" --ref="gpsdo" --pps="gpsdo"
./usrpMultiSample --dev="addr0=192.168.40.2, addr1=192.168.50.2" --file="/mnt/speedy/fieldTest" --duration="300" --rate="10e6" --freq="514.1666e6" --gain="70" --wait="0" --chan="6" --ref="gpsdo" --pps="gpsdo" --print="n" --spb="2"
