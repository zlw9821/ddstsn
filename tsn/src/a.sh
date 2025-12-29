#!/bin/bash

# 检查 root 权限
if [ "$EUID" -ne 0 ]; then 
  echo "请使用 sudo 运行此脚本"
  exit
fi

echo "--- 正在清理旧环境 ---"
ip netns del ns_pub 2>/dev/null
ip netns del ns_sub 2>/dev/null
ip link del tapa 2>/dev/null
ip link del tapb 2>/dev/null

echo "--- 创建命名空间 ---"
ip netns add ns_pub
ip netns add ns_sub

echo "--- 创建并分配 TAP 设备 ---"
# 创建 tapa 并放入 ns_pub
ip tuntap add mode tap dev tapa
ip link set tapa netns ns_pub

# 创建 tapb 并放入 ns_sub
ip tuntap add mode tap dev tapb
ip link set tapb netns ns_sub

echo "--- 配置 IP 地址与状态 ---"
# 配置 Pub 端 (192.168.2.20)
ip netns exec ns_pub ip addr add 192.168.2.20/24 dev tapa
ip netns exec ns_pub ip link set tapa up
ip netns exec ns_pub ip link set lo up

# 配置 Sub 端 (192.168.3.20)
ip netns exec ns_sub ip addr add 192.168.3.20/24 dev tapb
ip netns exec ns_sub ip link set tapb up
ip netns exec ns_sub ip link set lo up

echo "--- 强制路由隔离 (确保流量必经 TAP) ---"
# 告诉 ns_pub：去往 3.0 网段的消息必须扔给 tapa
ip netns exec ns_pub ip route add 192.168.3.0/24 dev tapa
# 告诉 ns_sub：去往 2.0 网段的消息必须扔给 tapb
ip netns exec ns_sub ip route add 192.168.2.0/24 dev tapb

echo "--- 优化仿真性能 (关闭 Checksum Offloading) ---"
# 必须关闭校验和卸载，否则 OMNeT++ 可能会因为计算错误丢弃这些包
ip netns exec ns_pub ethtool -K tapa tx off rx off 2>/dev/null
ip netns exec ns_sub ethtool -K tapb tx off rx off 2>/dev/null

echo "--- 网络拓扑准备就绪 ---"
echo "NS_PUB (tapa): 192.168.2.20"
echo "NS_SUB (tapb): 192.168.3.20"