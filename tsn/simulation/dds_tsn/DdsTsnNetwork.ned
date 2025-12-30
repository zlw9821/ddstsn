package simulation.dds_tsn;

import inet.networks.base.TsnNetworkBase;
import inet.node.tsn.TsnDevice;
import inet.node.tsn.TsnSwitch;
import inet.node.ethernet.Eth100M;

//
// DDS over TSN 
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
                @display("p=290,319;i=device/pc");
                numEthInterfaces = 1; 
        }

        // TSN 交换机
        switch1: TsnSwitch {
            parameters:
                @display("p=434,247;i=abstract/switch");
                numEthInterfaces = 2;
        }

        // Subscriber 端 TSN 网关设备
        host2: TsnDevice {
            parameters:
                @display("p=582,319;i=device/pc");
                numEthInterfaces = 1;
        }

    connections allowunconnected:
        host1.ethg[0] <--> Eth100M <--> switch1.ethg[0];
        host2.ethg[0] <--> Eth100M <--> switch1.ethg[1];
}
