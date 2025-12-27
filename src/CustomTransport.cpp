#include "CustomTransport.hpp"

#include <cstring>

#include "CustomTransportDescriptor.hpp"

using namespace eprosima::fastdds::rtps;

CustomTransport::CustomTransport(const CustomTransportDescriptor& descriptor)
    : TransportInterface(descriptor.transport_kind_),
      max_message_size_(descriptor.max_message_size_),
      descriptor_(&descriptor) {}

CustomTransport::~CustomTransport() { shutdown(); }

bool CustomTransport::send(eprosima::fastdds::rtps::Locator_t* /*locators*/,
                           const uint8_t* buffer, uint32_t size,
                           eprosima::fastdds::rtps::Locator_t* remote_locator) {
  // DDS 调用此函数发送数据
  if (size > max_message_size_) return false;

  RawPacket pkt;
  pkt.data.assign(buffer, buffer + size);
  if (remote_locator) pkt.remote = *remote_locator;

  std::lock_guard<std::mutex> lk(mtx_out_);
  outgoing_.push(std::move(pkt));
  // Set a simple notification flag and call optional callback. Keep calls
  // lightweight: callbacks may be invoked in DDS threads and must not touch
  // OMNeT++ objects directly.
  if (on_send_callback_) {
    try {
      on_send_callback_();
    } catch (...) {
      // guard against exceptions in user callbacks
    }
  }
  return true;
}

void CustomTransport::push_incoming(RawPacket&& pkt) {
  // App 收到 Socket 数据后调用此函数
  std::lock_guard<std::mutex> lk(mtx_receiver_);
  if (receiver_) {
    // 直接在当前线程（App的主线程）回调 Fast DDS
    // 这保证了仿真时间的原子性
    receiver_->OnDataReceived(
        reinterpret_cast<const eprosima::fastdds::rtps::octet*>(
            pkt.data.data()),
        static_cast<uint32_t>(pkt.data.size()), pkt.local, pkt.remote);
  }
}

bool CustomTransport::pop_outgoing(RawPacket& pkt) {
  std::lock_guard<std::mutex> lk(mtx_out_);
  if (outgoing_.empty()) return false;
  pkt = std::move(outgoing_.front());
  outgoing_.pop();
  return true;
}

bool CustomTransport::OpenInputChannel(
    const eprosima::fastdds::rtps::Locator& /*locator*/,
    eprosima::fastdds::rtps::TransportReceiverInterface* receiver,
    uint32_t /*maxMsgSz*/) {
  std::lock_guard<std::mutex> lk(mtx_receiver_);
  receiver_ = receiver;
  return true;
}

// TransportDescriptorInterface::create_transport returns a raw pointer
TransportInterface* CustomTransportDescriptor::create_transport() const {
  auto t = new CustomTransport(*this);
  // record the created instance (non-owning)
  transport_instance_.store(t);
  return t;
}

LocatorList CustomTransport::NormalizeLocator(const Locator& locator) {
  LocatorList l;
  l.push_back(locator);
  return l;
}

TransportDescriptorInterface* CustomTransport::get_configuration() {
  return const_cast<CustomTransportDescriptor*>(descriptor_);
}
