
sudo apt update
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
sudo apt install vlc vlc-plugin-access-extra libavcodec-extra
sudo apt install build-essential libc6-dev libstdc++-12-dev

# create TAP interfaces
sudo ip tuntap add mode tap dev tapa
sudo ip tuntap add mode tap dev tapb

# assign IP addresses to interfaces
sudo ip addr add 192.168.2.20/24 dev tapa
sudo ip addr add 192.168.3.20/24 dev tapb

# bring up all interfaces
sudo ip link set dev tapa up
sudo ip link set dev tapb up
