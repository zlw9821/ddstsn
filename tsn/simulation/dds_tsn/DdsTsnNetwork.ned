package simulation.dds_tsn;

import inet.node.tsn.TsnDevice;
import inet.node.tsn.TsnSwitch;
import inet.node.ethernet.Eth100M;

network DdsTsnNetwork
{
    submodules:
        host1: TsnDevice {
            parameters:
                @display("p=100,100");
                numEthInterfaces = 1;   // 保证有 eth[0]
        }
        host2: TsnDevice {
            parameters:
                @display("p=100,300");
                numEthInterfaces = 1;
        }
        tsnSwitch: TsnSwitch {
            parameters:
                @display("p=300,200");
        }

    connections allowunconnected:
        host1.ethg[0] <--> Eth100M { delay = 0.1us; } <--> tsnSwitch.ethg++;
        host2.ethg[0] <--> Eth100M { delay = 0.1us; } <--> tsnSwitch.ethg++;
}
