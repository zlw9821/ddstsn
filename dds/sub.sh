#! /bin/bash
export FASTDDS_DEFAULT_PROFILES_FILE=/home/parallels/dds_over_tsn/ddstsn/dds/sub.xml
sudo ip netns exec ns_sub ./dds_sub