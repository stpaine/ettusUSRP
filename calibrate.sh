#!/bin/bash

# addr0=192.168.40.2
# addr1=192.168.50.2
# 2TB NVME /mnt/speedy
# 4TB SSD /mnt/fatty

# Select root folder
folder_name=/mnt/speedy/

# Get date and time
var="$(date +%F)"-"$(date +%T)"
# Make colons dashes
var="${var//:/-}"
# Remove the dashes
var="${var//-}"

# Create final folder name
folder_name+="$var"

# Create folder
mkdir "$folder_name"

# DVB Calibration
./usrpMultiSample --dev="addr0=192.168.40.2, addr1=192.168.50.2" --file="$folder_name/calibration" --duration="60" --rate="2.5e6" --freq="223.936e6" --gainAll="68" --wait="120" --chan="6" --ref="gpsdo" --pps="gpsdo" --print="n" --spb="2"
