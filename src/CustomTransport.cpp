#include "CustomTransport.hpp"

#include <cstring>

#include "CustomTransportDescriptor.hpp"

using namespace eprosima::fastdds::rtps;

CustomTransport::CustomTransport(const CustomTransportDescriptor& descriptor)
    : TransportInterface(descriptor.transport_kind_),
      max_message_size_(descriptor.max_message_size_),
      descriptor_(&descriptor) {}

CustomTransport::~CustomTransport() { shutdown(); }

bool CustomTransport::init(
    const eprosima::fastdds::rtps::PropertyPolicy* /*properties*/,
    const uint32_t& /*max_msg_size_no_frag*/) {
  running_.store(true);
  worker_ = std::thread(&CustomTransport::worker_loop, this);
  return true;
}

void CustomTransport::shutdown() {
  if (running_.exchange(false)) {
    cv_in_.notify_all();
    if (worker_.joinable()) worker_.join();
  }
}

bool CustomTransport::send(Locator_t* /*locators*/, const uint8_t* buffer,
                           uint32_t size, Locator_t* remote_locator) {
  if (size > max_message_size_) return false;

  RawPacket pkt;
  pkt.data.assign(buffer, buffer + size);
  if (remote_locator) pkt.remote = *remote_locator;

  {
    std::lock_guard<std::mutex> lk(mtx_out_);
    outgoing_.push(std::move(pkt));
  }
  return true;
}

// Not part of TransportInterface, helper
void CustomTransport::set_receive_callback(
    TransportReceiverInterface* receiver) {
  std::lock_guard<std::mutex> lk(mtx_receiver_);
  receiver_ = receiver;
}

bool CustomTransport::OpenInputChannel(const Locator& /*locator*/,
                                       TransportReceiverInterface* receiver,
                                       uint32_t /*maxMsgSz*/) {
  std::lock_guard<std::mutex> lk(mtx_receiver_);
  receiver_ = receiver;
  return true;
}

void CustomTransport::push_incoming(RawPacket&& pkt) {
  {
    std::lock_guard<std::mutex> lk(mtx_in_);
    incoming_.push(std::move(pkt));
  }
  cv_in_.notify_one();
}

bool CustomTransport::pop_outgoing(RawPacket& pkt) {
  std::lock_guard<std::mutex> lk(mtx_out_);
  if (outgoing_.empty()) return false;
  pkt = std::move(outgoing_.front());
  outgoing_.pop();
  return true;
}

void CustomTransport::worker_loop() {
  while (running_.load()) {
    std::unique_lock<std::mutex> lk(mtx_in_);
    cv_in_.wait(lk, [&] { return !incoming_.empty() || !running_.load(); });

    if (!running_.load()) break;

    while (!incoming_.empty()) {
      RawPacket pkt = std::move(incoming_.front());
      incoming_.pop();
      lk.unlock();

      // 线程安全地获取 receiver_ 指针
      TransportReceiverInterface* receiver = nullptr;
      {
        std::lock_guard<std::mutex> receiver_lk(mtx_receiver_);
        receiver = receiver_;
      }

      if (receiver) {
        receiver->OnDataReceived(
            reinterpret_cast<const octet*>(pkt.data.data()),
            static_cast<uint32_t>(pkt.data.size()), pkt.local, pkt.remote);
      }

      lk.lock();
    }
  }
}

// TransportDescriptorInterface::create_transport returns a raw pointer
TransportInterface* CustomTransportDescriptor::create_transport() const {
  auto t = new CustomTransport(*this);
  // record the created instance (non-owning)
  transport_instance_.store(t);
  return t;
}

// Minimal implementations for required virtual methods
bool CustomTransport::IsInputChannelOpen(const Locator& /*locator*/) const {
  return true;
}

bool CustomTransport::IsLocatorSupported(const Locator& /*locator*/) const {
  return true;
}

bool CustomTransport::is_locator_allowed(const Locator& /*locator*/) const {
  return true;
}

bool CustomTransport::is_locator_reachable(const Locator_t& /*locator*/) {
  return true;
}

Locator CustomTransport::RemoteToMainLocal(const Locator& remote) const {
  return remote;
}

bool CustomTransport::OpenOutputChannel(
    SendResourceList& /*sender_resource_list*/, const Locator& /*locator*/) {
  return true;
}

bool CustomTransport::CloseInputChannel(const Locator& /*locator*/) {
  return true;
}

bool CustomTransport::DoInputLocatorsMatch(const Locator& a,
                                           const Locator& b) const {
  return a == b;
}

LocatorList CustomTransport::NormalizeLocator(const Locator& locator) {
  LocatorList l;
  l.push_back(locator);
  return l;
}

void CustomTransport::select_locators(LocatorSelector& /*selector*/) const {}

bool CustomTransport::is_local_locator(const Locator& /*locator*/) const {
  return false;
}

TransportDescriptorInterface* CustomTransport::get_configuration() {
  return const_cast<CustomTransportDescriptor*>(descriptor_);
}

void CustomTransport::AddDefaultOutputLocator(LocatorList& /*defaultList*/) {}

bool CustomTransport::getDefaultMetatrafficMulticastLocators(
    LocatorList& /*locators*/, uint32_t /*metatraffic_multicast_port*/) const {
  return false;
}

bool CustomTransport::getDefaultMetatrafficUnicastLocators(
    LocatorList& /*locators*/, uint32_t /*metatraffic_unicast_port*/) const {
  return false;
}

bool CustomTransport::getDefaultUnicastLocators(
    LocatorList& /*locators*/, uint32_t /*unicast_port*/) const {
  return false;
}

bool CustomTransport::fillMetatrafficMulticastLocator(
    Locator& /*locator*/, uint32_t /*metatraffic_multicast_port*/) const {
  return false;
}

bool CustomTransport::fillMetatrafficUnicastLocator(
    Locator& /*locator*/, uint32_t /*metatraffic_unicast_port*/) const {
  return false;
}

bool CustomTransport::configureInitialPeerLocator(
    Locator& /*locator*/, const PortParameters& /*port_params*/,
    uint32_t /*domainId*/, LocatorList& /*list*/) const {
  return false;
}

bool CustomTransport::fillUnicastLocator(Locator& /*locator*/,
                                         uint32_t /*well_known_port*/) const {
  return false;
}

uint32_t CustomTransport::max_recv_buffer_size() const {
  return max_message_size_;
}
