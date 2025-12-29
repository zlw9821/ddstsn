#! /bin/bash
export FASTDDS_DEFAULT_PROFILES_FILE=/home/parallels/dds_over_tsn/ddstsn/dds/pub.xml
sudo ip netns exec ns_pub ./dds_pub
