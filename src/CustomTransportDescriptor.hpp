#pragma once

#include <atomic>
#include <cstdint>
#include <fastdds/rtps/transport/TransportDescriptorInterface.hpp>
#include <memory>

class CustomTransport;

class CustomTransportDescriptor
    : public eprosima::fastdds::rtps::TransportDescriptorInterface {
 public:
  CustomTransportDescriptor()
      : eprosima::fastdds::rtps::TransportDescriptorInterface(
            65536, eprosima::fastdds::rtps::s_maximumInitialPeersRange),
        max_message_size_(1450),
        transport_kind_(1000) {
    // 选择一个不会与内置 transport 冲突的 kind
  }

  // TransportDescriptorInterface::create_transport returns a raw pointer
  eprosima::fastdds::rtps::TransportInterface* create_transport()
      const override;

  uint32_t min_send_buffer_size() const override { return 1; }

  // If set, points to the last created transport instance (non-owning)
  CustomTransport* get_created_transport() const {
    return transport_instance_.load();
  }

  uint32_t max_message_size_ = 1450;
  int32_t transport_kind_ = 1000;

 private:
  mutable std::atomic<CustomTransport*> transport_instance_;
};
