
# create TAP interfaces
sudo ip tuntap add mode tap dev tapa
sudo ip tuntap add mode tap dev tapb

# assign IP addresses to interfaces
sudo ip addr add 192.168.2.20/24 dev tapa
sudo ip addr add 192.168.3.20/24 dev tapb

# bring up all interfaces
sudo ip link set dev tapa up
sudo ip link set dev tapb up

sudo ip netns exec ns_pub ethtool -K tapa tx off rx off
sudo ip netns exec ns_sub ethtool -K tapb tx off rx off

echo 0 > /sys/devices/virtual/net/br_pub/bridge/multicast_snooping 
echo 0 > /sys/devices/virtual/net/br_sub/bridge/multicast_snooping