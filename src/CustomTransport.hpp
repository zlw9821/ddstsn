#pragma once

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/rtps/transport/TransportInterface.hpp>
#include <fastdds/rtps/transport/TransportReceiverInterface.hpp>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// 传输原始包（Fast-DDS <-> OMNeT++ Bridge 之间）
struct RawPacket {
  std::vector<uint8_t> data;
  eprosima::fastdds::rtps::Locator_t local;
  eprosima::fastdds::rtps::Locator_t remote;

  RawPacket() {
    // 初始化 locators
    std::memset(&local, 0, sizeof(local));
    std::memset(&remote, 0, sizeof(remote));
  }
};

// forward declare descriptor
class CustomTransportDescriptor;

class CustomTransport : public eprosima::fastdds::rtps::TransportInterface {
 public:
  // Construct from our descriptor
  explicit CustomTransport(const CustomTransportDescriptor& descriptor);
  ~CustomTransport() override;

  // Transport lifecycle
  bool init(const eprosima::fastdds::rtps::PropertyPolicy* properties = nullptr,
            const uint32_t& max_msg_size_no_frag = 0) override;
  void shutdown() override;

  // Helper used by the bridge to enqueue outgoing packets (not a
  // TransportInterface method)
  bool send(eprosima::fastdds::rtps::Locator_t* locators, const uint8_t* buffer,
            uint32_t size, eprosima::fastdds::rtps::Locator_t* remote_locator);

  // Optional helper to set the receiver directly (also done via
  // OpenInputChannel)
  void set_receive_callback(
      eprosima::fastdds::rtps::TransportReceiverInterface* receiver);

  // Bridge -> Transport（收到 INET UDP 后调用）
  void push_incoming(RawPacket&& pkt);

  // Bridge 轮询 outgoing
  bool pop_outgoing(RawPacket& pkt);

  // TransportInterface required overrides
  bool IsInputChannelOpen(
      const eprosima::fastdds::rtps::Locator&) const override;
  bool IsLocatorSupported(
      const eprosima::fastdds::rtps::Locator&) const override;
  bool is_locator_allowed(
      const eprosima::fastdds::rtps::Locator&) const override;
  bool is_locator_reachable(
      const eprosima::fastdds::rtps::Locator_t& locator) override;
  eprosima::fastdds::rtps::Locator RemoteToMainLocal(
      const eprosima::fastdds::rtps::Locator& remote) const override;
  bool OpenOutputChannel(
      eprosima::fastdds::rtps::SendResourceList& sender_resource_list,
      const eprosima::fastdds::rtps::Locator&) override;
  bool OpenInputChannel(
      const eprosima::fastdds::rtps::Locator& locator,
      eprosima::fastdds::rtps::TransportReceiverInterface* receiver,
      uint32_t) override;
  bool CloseInputChannel(const eprosima::fastdds::rtps::Locator&) override;
  bool DoInputLocatorsMatch(
      const eprosima::fastdds::rtps::Locator&,
      const eprosima::fastdds::rtps::Locator&) const override;
  eprosima::fastdds::rtps::LocatorList NormalizeLocator(
      const eprosima::fastdds::rtps::Locator& locator) override;
  void select_locators(
      eprosima::fastdds::rtps::LocatorSelector& selector) const override;
  bool is_local_locator(
      const eprosima::fastdds::rtps::Locator& locator) const override;
  eprosima::fastdds::rtps::TransportDescriptorInterface* get_configuration()
      override;
  void AddDefaultOutputLocator(
      eprosima::fastdds::rtps::LocatorList& defaultList) override;
  bool getDefaultMetatrafficMulticastLocators(
      eprosima::fastdds::rtps::LocatorList& locators,
      uint32_t metatraffic_multicast_port) const override;
  bool getDefaultMetatrafficUnicastLocators(
      eprosima::fastdds::rtps::LocatorList& locators,
      uint32_t metatraffic_unicast_port) const override;
  bool getDefaultUnicastLocators(eprosima::fastdds::rtps::LocatorList& locators,
                                 uint32_t unicast_port) const override;
  bool fillMetatrafficMulticastLocator(
      eprosima::fastdds::rtps::Locator& locator,
      uint32_t metatraffic_multicast_port) const override;
  bool fillMetatrafficUnicastLocator(
      eprosima::fastdds::rtps::Locator& locator,
      uint32_t metatraffic_unicast_port) const override;
  bool configureInitialPeerLocator(
      eprosima::fastdds::rtps::Locator& locator,
      const eprosima::fastdds::rtps::PortParameters& port_params,
      uint32_t domainId,
      eprosima::fastdds::rtps::LocatorList& list) const override;
  bool fillUnicastLocator(eprosima::fastdds::rtps::Locator& locator,
                          uint32_t well_known_port) const override;
  uint32_t max_recv_buffer_size() const override;

 private:
  void worker_loop();

 private:
  uint32_t max_message_size_;

  // incoming: Bridge -> worker thread -> RTPS
  std::mutex mtx_in_;
  std::condition_variable cv_in_;
  std::queue<RawPacket> incoming_;

  // outgoing: RTPS -> Bridge
  std::mutex mtx_out_;
  std::queue<RawPacket> outgoing_;

  std::thread worker_;
  std::atomic_bool running_{false};

  // receiver_ 的访问需要同步保护
  std::mutex mtx_receiver_;
  eprosima::fastdds::rtps::TransportReceiverInterface* receiver_{nullptr};

  // keep pointer to our descriptor (owned by the user via shared_ptr)
  const CustomTransportDescriptor* descriptor_ = nullptr;
};
