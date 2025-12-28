package simulation.ddstsndemo;

import inet.networks.base.TsnNetworkBase;
import inet.node.contract.IEthernetNetworkNode;
import inet.node.ethernet.EthernetLink;

import inet.applications.contract.IApp;

simple DdsTsnPublisherApp like IApp
{
    parameters:
        string destAddress = default("server");
        int destPort;
        int localPort = default(-1);
        double startTime @unit(s) = default(0s);
        double stopTime @unit(s) = default(-1s);
        double sendInterval @unit(s) = default(1s);
        string message = default("Hello TSN DDS");
        // Range or comma-separated list, e.g. "7400-7450,8000"
        string discoveryPorts = default("7400-7450");
        // Max DDS message size (bytes). If set > network MTU and fragmentation
        // is not enabled in INET's UDP layer, packets may be dropped by
        // lower layers. Default -1 = use transport descriptor default.
        int maxMessageSize = default(-1);
        // If >0, scale DDS timeouts (heartbeat, lease, etc.) by this factor to
        // tolerate simulation slowdowns. For example, set to 10 to make timers
        // 10x more lenient. Default 1 = no scaling.
        double qosScaleFactor = default(1.0);
        @display("i=block/app");
    gates:
        input socketIn;
        output socketOut;
}

simple DdsTsnSubscriberApp like IApp
{
    parameters:
        int localPort;
        double pollInterval @unit(s) = default(0.01s);
        // If >0, scale DDS timeouts (heartbeat, lease, etc.) by this factor to
        // tolerate simulation slowdowns. Default 1 = no scaling.
        double qosScaleFactor = default(1.0);
        // Optional: if true, log INET LabelsTag / VLAN info from incoming packets
        bool logStreamTags = default(true);
        @display("i=block/app");
    gates:
        input socketIn;
        output socketOut;
}

simple FastDdsOmnetBridge
{
    parameters:
        int localPort;
        string peerAddress;
        int peerPort;
        double pollInterval @unit(s) = default(0.001s);
    gates:
        output socketOut;
        input socketIn;
}


network DdsTsnDemoNetwork extends TsnNetworkBase
{
    parameters:
        *.eth[*].bitrate = default(100Mbps);
    submodules:
        client: <default("TsnDevice")> like IEthernetNetworkNode {
            @display("p=350,200");
        }
        switch: <default("TsnSwitch")> like IEthernetNetworkNode {
            @display("p=550,200");
        }
        server: <default("TsnDevice")> like IEthernetNetworkNode {
            @display("p=750,200");
        }
    connections:
        client.ethg++ <--> EthernetLink <--> switch.ethg++;
        switch.ethg++ <--> EthernetLink <--> server.ethg++;
}

