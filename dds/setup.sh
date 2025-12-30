#!/bin/bash

# 确保以 root 权限运行
if [ "$EUID" -ne 0 ]; then
  echo "错误: 请使用 sudo 运行此脚本"
  exit 1
fi

set -e  # 出错即停止

echo "=== 1. 清理旧环境 ==="
# 停止并删除 netns
ip netns del ns_pub 2>/dev/null || true
ip netns del ns_sub 2>/dev/null || true

# 删除网桥和虚拟网卡
ip link del br_pub 2>/dev/null || true
ip link del br_sub 2>/dev/null || true
ip link del veth_pub 2>/dev/null || true
ip link del veth_sub 2>/dev/null || true
ip link del tapa 2>/dev/null || true
ip link del tapb 2>/dev/null || true

echo "=== 2. 创建 Network Namespace ==="
ip netns add ns_pub
ip netns add ns_sub

echo "=== 3. 创建并配置网桥 (Bridge) ==="
# 创建网桥但不给网桥分配 IP，仅作为二层交换机
ip link add br_pub type bridge
ip link add br_sub type bridge
ip link set br_pub up
ip link set br_sub up

echo "=== 4. 创建 TAP 设备 (对接 INET) ==="
# 创建 TAP 并挂载到网桥
ip tuntap add mode tap dev tapa
ip link set tapa up
ip link set tapa master br_pub

ip tuntap add mode tap dev tapb
ip link set tapb up
ip link set tapb master br_sub

echo "=== 5. 创建 VETH 对并连接 Namespace ==="
# Publisher 侧连接
ip link add veth_pub type veth peer name veth_pub_ns
ip link set veth_pub master br_pub
ip link set veth_pub up
ip link set veth_pub_ns netns ns_pub

# Subscriber 侧连接
ip link add veth_sub type veth peer name veth_sub_ns
ip link set veth_sub master br_sub
ip link set veth_sub up
ip link set veth_sub_ns netns ns_sub

echo "=== 6. 配置 Namespace 内部网络 ==="
# 配置 Publisher 端点 (192.168.2.20)
ip netns exec ns_pub ip addr add 192.168.2.20/24 dev veth_pub_ns
ip netns exec ns_pub ip link set veth_pub_ns up
ip netns exec ns_pub ip link set lo up
# DDS 发现需要组播，确保有默认路由或特定路由指向接口
ip netns exec ns_pub ip route add default dev veth_pub_ns

# 配置 Subscriber 端点 (192.168.2.21)
ip netns exec ns_sub ip addr add 192.168.2.21/24 dev veth_sub_ns
ip netns exec ns_sub ip link set veth_sub_ns up
ip netns exec ns_sub ip link set lo up
ip netns exec ns_sub ip route add default dev veth_sub_ns

echo "=== 7. 禁用 Checksum Offloading (关键) ==="
# 仿真器通常无法处理硬件校验和卸载，必须在两端都关闭
for dev in tapa tapb; do
    ethtool -K $dev tx off rx off 2>/dev/null || true
done

ip netns exec ns_pub ethtool -K veth_pub_ns tx off rx off 2>/dev/null || true
ip netns exec ns_sub ethtool -K veth_sub_ns tx off rx off 2>/dev/null || true

echo 0 > /sys/devices/virtual/net/br_pub/bridge/multicast_snooping 
echo 0 > /sys/devices/virtual/net/br_sub/bridge/multicast_snooping

ip netns exec ns_pub ip route add 239.0.0.0/8 dev veth_pub_ns
ip netns exec ns_sub ip route add 239.0.0.0/8 dev veth_sub_ns

echo "=== 部署完成 ==="
echo "-------------------------------------------------------"
echo "逻辑架构:"
echo "  [ns_pub: .20] <-> veth_pub_ns <-> [br_pub] <-> tapa <-> (INET)"
echo "  [ns_sub: .21] <-> veth_sub_ns <-> [br_sub] <-> tapb <-> (INET)"
echo "-------------------------------------------------------"
echo "测试建议:"
echo "  1. 在 ns_pub 运行: sudo ip netns exec ns_pub ping 192.168.2.21"
echo "  2. 此时 INET 仿真器应在 tapa/tapb 之间转发流量，否则 ping 不通。"