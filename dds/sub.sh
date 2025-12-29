#! /bin/bash
export FASTDDS_DEFAULT_PROFILES_FILE=sub.xml
sudo ip netns exec ns_sub ./dds_sub