// CustomTransport.hpp
#pragma once

#include <atomic>
#include <cstring>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/rtps/transport/TransportInterface.hpp>
#include <fastdds/rtps/transport/TransportReceiverInterface.hpp>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>

// 传输原始包结构
struct RawPacket {
  std::vector<uint8_t> data;
  eprosima::fastdds::rtps::Locator_t local;
  eprosima::fastdds::rtps::Locator_t remote;

  RawPacket() {
    std::memset(&local, 0, sizeof(local));
    std::memset(&remote, 0, sizeof(remote));
  }
};

class CustomTransportDescriptor;

class CustomTransport : public eprosima::fastdds::rtps::TransportInterface {
 public:
  explicit CustomTransport(const CustomTransportDescriptor& descriptor);
  ~CustomTransport() override;

  bool init(const eprosima::fastdds::rtps::PropertyPolicy* = nullptr,
            const uint32_t& = 0) override {
    return true;
  }
  void shutdown() override {}

  // DDS -> Transport: DDS 想要发送数据
  bool send(
      eprosima::fastdds::rtps::Locator_t* locators, const uint8_t* buffer,
      uint32_t size,
      eprosima::fastdds::rtps::Locator_t* remote_locator);  // 注意 override

  // App -> Transport: 网络收到数据，推入 DDS
  // 注意：在仿真中，我们直接同步回调 receiver，不需要中间队列
  void push_incoming(RawPacket&& pkt);

  // App -> Transport: 轮询 DDS 是否有要发出的数据
  bool pop_outgoing(RawPacket& pkt);

  // Set a lightweight callback to notify upper layers when outgoing data
  // is available. Callback must be thread-safe and must not call OMNeT++ APIs.
  void set_on_send_callback(std::function<void()> cb) {
    on_send_callback_ = cb;
  }

  // TransportInterface 必须实现的接口
  bool IsInputChannelOpen(
      const eprosima::fastdds::rtps::Locator&) const override {
    return true;
  }
  bool IsLocatorSupported(
      const eprosima::fastdds::rtps::Locator&) const override {
    return true;
  }
  bool is_locator_allowed(
      const eprosima::fastdds::rtps::Locator&) const override {
    return true;
  }
  bool is_locator_reachable(
      const eprosima::fastdds::rtps::Locator_t&) override {
    return true;
  }
  eprosima::fastdds::rtps::Locator RemoteToMainLocal(
      const eprosima::fastdds::rtps::Locator& remote) const override {
    return remote;
  }
  bool OpenOutputChannel(eprosima::fastdds::rtps::SendResourceList&,
                         const eprosima::fastdds::rtps::Locator&) override {
    return true;
  }

  // 打开接收通道时记录 receiver 指针
  bool OpenInputChannel(
      const eprosima::fastdds::rtps::Locator&,
      eprosima::fastdds::rtps::TransportReceiverInterface* receiver,
      uint32_t) override;

  bool CloseInputChannel(const eprosima::fastdds::rtps::Locator&) override {
    return true;
  }
  bool DoInputLocatorsMatch(
      const eprosima::fastdds::rtps::Locator&,
      const eprosima::fastdds::rtps::Locator&) const override {
    return true;
  }
  eprosima::fastdds::rtps::LocatorList NormalizeLocator(
      const eprosima::fastdds::rtps::Locator& locator) override;
  void select_locators(
      eprosima::fastdds::rtps::LocatorSelector&) const override {}
  bool is_local_locator(
      const eprosima::fastdds::rtps::Locator&) const override {
    return false;
  }
  eprosima::fastdds::rtps::TransportDescriptorInterface* get_configuration()
      override;
  void AddDefaultOutputLocator(eprosima::fastdds::rtps::LocatorList&) override {
  }
  bool getDefaultMetatrafficMulticastLocators(
      eprosima::fastdds::rtps::LocatorList&, uint32_t) const override {
    return false;
  }
  bool getDefaultMetatrafficUnicastLocators(
      eprosima::fastdds::rtps::LocatorList&, uint32_t) const override {
    return false;
  }
  bool getDefaultUnicastLocators(eprosima::fastdds::rtps::LocatorList&,
                                 uint32_t) const override {
    return false;
  }
  bool fillMetatrafficMulticastLocator(eprosima::fastdds::rtps::Locator&,
                                       uint32_t) const override {
    return false;
  }
  bool fillMetatrafficUnicastLocator(eprosima::fastdds::rtps::Locator&,
                                     uint32_t) const override {
    return false;
  }
  bool configureInitialPeerLocator(
      eprosima::fastdds::rtps::Locator&,
      const eprosima::fastdds::rtps::PortParameters&, uint32_t,
      eprosima::fastdds::rtps::LocatorList&) const override {
    return false;
  }
  bool fillUnicastLocator(eprosima::fastdds::rtps::Locator&,
                          uint32_t) const override {
    return false;
  }
  uint32_t max_recv_buffer_size() const override { return max_message_size_; }

 private:
  uint32_t max_message_size_;

  // Outgoing queue (FastDDS -> Bridge)
  std::mutex mtx_out_;
  std::queue<RawPacket> outgoing_;

  // Receiver callback (Bridge -> FastDDS)
  std::mutex mtx_receiver_;
  eprosima::fastdds::rtps::TransportReceiverInterface* receiver_{nullptr};

  // Optional callback to notify upper layer that outgoing data is available.
  // WARNING: Fast DDS may call send() from its internal threads. Callbacks
  // may therefore be invoked from DDS threads and MUST NOT call OMNeT++ API
  // directly. Use them to set thread-safe flags only, or implement a safe
  // notification mechanism.
  std::function<void()> on_send_callback_ = nullptr;

  const CustomTransportDescriptor* descriptor_ = nullptr;
};