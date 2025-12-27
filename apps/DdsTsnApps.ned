
package apps;

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


network DdsTsnNetwork extends TsnNetworkBase
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

