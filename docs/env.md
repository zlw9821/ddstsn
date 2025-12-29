
## Install Fast DDS

```bash
mkdir ~/Fast-DDS

cd ~/Fast-DDS
git clone https://github.com/eProsima/foonathan_memory_vendor.git
mkdir foonathan_memory_vendor/build
cd foonathan_memory_vendor/build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON
cmake --build . --target install

cd ~/Fast-DDS
git clone https://github.com/eProsima/Fast-CDR.git
mkdir Fast-CDR/build
cd Fast-CDR/build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --target install

cd ~/Fast-DDS
git clone https://github.com/eProsima/Fast-DDS.git
mkdir Fast-DDS/build
cd Fast-DDS/build
cmake ..  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --target install

cd ~/Fast-DDS
git clone https://github.com/foonathan/memory.git
cd memory
mkdir build && cd build
cmake .. -DFOONATHAN_MEMORY_BUILD_EXAMPLES=OFF -DFOONATHAN_MEMORY_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --target install

echo 'export LD_LIBRARY_PATH=/usr/local/lib/' >> ~/.bashrc
export PATH=$FAST_DDS_INSTALL_ROOT/bin:/opt/fastddsgen/scripts:/root/omnetpp/dds_over_tsn/inet/bin:/root/omnetpp/omnetpp-6.3.0/bin:$PATH

```
