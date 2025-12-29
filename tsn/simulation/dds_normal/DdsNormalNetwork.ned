package simulation.dds_normal;

import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.ethernet.Eth100M;
import inet.node.ethernet.EthernetSwitch;
import inet.node.inet.StandardHost;
import inet.visualizer.common.IntegratedVisualizer;

network DdsNormalNetwork
{
    parameters:
        @display("bgb=800,400");
    submodules:
        configurator: Ipv4NetworkConfigurator {
            parameters:
                @display("p=100,50;is=s");
        }
        visualizer: IntegratedVisualizer {
            parameters:
                @display("p=100,150;is=s");
        }
        switch1: EthernetSwitch {
            parameters:
                @display("p=400,200");
        }
        host1: StandardHost {
            parameters:
                @display("p=200,300");
        }
        host2: StandardHost {
            parameters:
                @display("p=600,300");
        }
    connections:
        host1.ethg++ <--> Eth100M <--> switch1.ethg++;
        host2.ethg++ <--> Eth100M <--> switch1.ethg++;
}