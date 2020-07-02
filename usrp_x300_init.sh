#!/bin/bash
#
echo "-- Initialising USRP X300 devices.."

# Search for devices
uhd_find_devices

# Set MTU size
# https://www.cyberciti.biz/faq/how-can-i-setup-the-mtu-for-my-network-interface/
sudo ip link set dev ens5f0 mtu 9202
sudo ip link set dev ens5f1 mtu 9202

# Set buffer size
# https://ixnfo.com/en/changing-tx-and-rx-network-interface-buffers-in-linux.html
sudo ethtool -G ens5f0 rx 4092 tx 4092
sudo ethtool -G ens5f1 rx 4092 tx 4092

sudo sysctl -w net.core.rmem_max=50000000
sudo sysctl -w net.core.wmem_max=50000000
echo "-- Done!"
