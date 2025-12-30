#!/bin/bash

# 确保以 root 权限运行
if [ "$EUID" -ne 0 ]; then
  echo "错误: 请使用 sudo 运行此脚本"
  exit 1
fi

set -e

echo "=== 1. 清理旧环境 ==="
ip netns del ns_pub 2>/dev/null || true
ip netns del ns_sub 2>/dev/null || true
ip link del br_pub 2>/dev/null || true
ip link del br_sub 2>/dev/null || true
ip link del veth_pub 2>/dev/null || true
ip link del veth_sub 2>/dev/null || true
ip link del tapa 2>/dev/null || true
ip link del tapb 2>/dev/null || true

echo "=== 2. 创建 Network Namespace ==="
ip netns add ns_pub
ip netns add ns_sub

echo "=== 3. 创建并配置网桥 ==="
ip link add br_pub type bridge
ip link add br_sub type bridge
ip link set br_pub up
ip link set br_sub up

echo "=== 4. 创建 TAP 设备 (对接 OMNeT++) ==="
ip tuntap add mode tap dev tapa
ip link set tapa up
ip link set tapa master br_pub

ip tuntap add mode tap dev tapb
ip link set tapb up
ip link set tapb master br_sub

echo "=== 5. 创建 VETH 对并连接 Namespace ==="
ip link add veth_pub type veth peer name veth_pub_ns
ip link set veth_pub master br_pub
ip link set veth_pub up
ip link set veth_pub_ns netns ns_pub

ip link add veth_sub type veth peer name veth_sub_ns
ip link set veth_sub master br_sub
ip link set veth_sub up
ip link set veth_sub_ns netns ns_sub

echo "=== 6. 配置 Namespace 内部网络 ==="
# Publisher 端 (192.168.2.20)
ip netns exec ns_pub ip addr add 192.168.2.20/24 dev veth_pub_ns
ip netns exec ns_pub ip link set veth_pub_ns up
ip netns exec ns_pub ip link set lo up
ip netns exec ns_pub ip route add default dev veth_pub_ns

# Subscriber 端 (192.168.2.21)
ip netns exec ns_sub ip addr add 192.168.2.21/24 dev veth_sub_ns
ip netns exec ns_sub ip link set veth_sub_ns up
ip netns exec ns_sub ip link set lo up
ip netns exec ns_sub ip route add default dev veth_sub_ns

echo "=== 7. 禁用 Checksum Offloading ==="
for dev in tapa tapb; do
    ethtool -K $dev tx off rx off 2>/dev/null || true
done

ip netns exec ns_pub ethtool -K veth_pub_ns tx off rx off 2>/dev/null || true
ip netns exec ns_sub ethtool -K veth_sub_ns tx off rx off 2>/dev/null || true

echo "=== 8. 禁用组播侦听 ==="
echo 0 > /sys/devices/virtual/net/br_pub/bridge/multicast_snooping 
echo 0 > /sys/devices/virtual/net/br_sub/bridge/multicast_snooping

echo "=== 9. 配置组播路由 ==="
# 添加组播路由 (DDS 使用 239.255.0.1)
ip netns exec ns_pub ip route add 239.0.0.0/8 dev veth_pub_ns
ip netns exec ns_sub ip route add 239.0.0.0/8 dev veth_sub_ns

echo "=== 部署完成 ==="
echo "-------------------------------------------------------"
echo "网络拓扑:"
echo "  [ns_pub:192.168.2.20] <-> br_pub <-> tapa <-> [OMNeT++ host1]"
echo "  [OMNeT++ host1] <-> switch1 <-> [OMNeT++ host2]"
echo "  [OMNeT++ host2] <-> tapb <-> br_sub <-> [ns_sub:192.168.2.21]"
echo "-------------------------------------------------------"
echo "DDS 配置:"
echo "  - 组播地址: 239.255.0.1"
echo "  - Meta流量端口: 7400"
echo "  - 用户数据端口: 7412"
echo "-------------------------------------------------------"