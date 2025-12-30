package simulation.dds_tsn;

import inet.networks.base.TsnNetworkBase;
import inet.node.tsn.TsnDevice;
import inet.node.tsn.TsnSwitch;
import inet.node.ethernet.Eth100M;

//
// DDS over TSN 最小仿真网络 - 使用 TSN 设备
// 
// 拓扑: [ns_pub via tapa] <-> host1(TSN) <-> switch1(TSN) <-> host2(TSN) <-> [ns_sub via tapb]
//
network DdsTsnNetwork extends TsnNetworkBase
{
    parameters:
        @display("bgb=800,400");

    submodules:
        // Publisher 端 TSN 网关设备
        host1: TsnDevice {
            parameters:
                @display("p=200,200;i=device/pc");
                numEthInterfaces = 2;  // eth[0]=TAP, eth[1]=TSN链路
        }

        // TSN 交换机
        switch1: TsnSwitch {
            parameters:
                @display("p=400,200;i=abstract/switch");
                numEthInterfaces = 2;
        }

        // Subscriber 端 TSN 网关设备
        host2: TsnDevice {
            parameters:
                @display("p=600,200;i=device/pc");
                numEthInterfaces = 2;  // eth[0]=TAP, eth[1]=TSN链路
        }

    connections allowunconnected:
        // TSN 骨干链路 (100 Mbps 全双工)
        // eth[0] 保留给外部 TAP 设备
        // eth[1] 用于 TSN 网络互联
        host1.ethg[1] <--> Eth100M <--> switch1.ethg[0];
        host2.ethg[1] <--> Eth100M <--> switch1.ethg[1];
}