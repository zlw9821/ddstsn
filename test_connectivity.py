import socket
import struct
import sys
import time

def start_test(role, ip_addr):
    # 模拟 DDS 使用的组播地址
    MCAST_GRP = '239.255.0.1'
    MCAST_PORT = 7400
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    if role == "sub":
        # Subscriber 逻辑：监听组播
        sock.bind(('', MCAST_PORT))
        mreq = struct.pack("4sl", socket.inet_aton(MCAST_GRP), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        print(f"[*] Subscriber ({ip_addr}) 正在等待数据...")
        while True:
            data, addr = sock.recvfrom(1024)
            try:
                msg = data.decode()
                print(f"[!] 收到来自 {addr} 的消息: {msg}")
            except UnicodeDecodeError:
                msg = "<无法解码的消息>"
    else:
        # Publisher 逻辑：发送组播
        print(f"[*] Publisher ({ip_addr}) 正在发送心跳...")
        # 绑定到特定接口 IP
        sock.bind((ip_addr, 0))
        # 设置组播 TTL
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
        while True:
            message = f"Hello from Publisher at {time.strftime('%H:%M:%S')}"
            sock.sendto(message.encode(), (MCAST_GRP, MCAST_PORT))
            print(f"[>] 已发送: {message}")
            time.sleep(2)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("用法: python3 test.py <pub|sub> <local_ip>")
        sys.exit(1)
    start_test(sys.argv[1], sys.argv[2])