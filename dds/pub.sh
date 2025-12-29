#! /bin/bash
export FASTDDS_DEFAULT_PROFILES_FILE=pub.xml
sudo ip netns exec ns_pub ./dds_pub
