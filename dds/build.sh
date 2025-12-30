#! /bin/bash
set -e

current_dir=$(cd $(dirname $0); pwd)
cd $current_dir

rm -rf build
mkdir build
cd build
cmake .. 
make -j4
cd ..
cp build/dds_* .
