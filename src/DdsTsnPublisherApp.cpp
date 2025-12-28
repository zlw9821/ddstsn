#include <omnetpp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/rtps/transport/TransportInterface.hpp>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "CustomTransport.hpp"
#include "CustomTransportDescriptor.hpp"
#include "HelloWorldPubSubTypes.hpp"
#include "inet/common/InitStages.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/linklayer/common/VlanTag_m.h"  // used to request VLAN/PCP on egress
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/queueing/common/LabelsTag_m.h"  // used to label packets with TSN stream
#include "inet/transportlayer/contract/udp/UdpSocket.h"

// helper to convert byte sequences to hex string
static std::string bytes_to_hex(const uint8_t* data, size_t len) {
  std::ostringstream ss;
  ss.setf(std::ios::hex, std::ios::basefield);
  ss.fill('0');
  for (size_t i = 0; i < len; ++i)
    ss << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
  return ss.str();
}

using namespace omnetpp;
using namespace inet;
using namespace eprosima::fastdds::dds;
using namespace eprosima::fastdds::rtps;

// Helper: attempt to extract an IPv4 or IPv6 textual address from Locator_t.
static std::string locator_to_ipstring(
    const eprosima::fastdds::rtps::Locator_t& loc) {
  const unsigned char* addr =
      reinterpret_cast<const unsigned char*>(loc.address);
  // Heuristic for IPv4: last 4 bytes non-zero (common for IPv4-mapped
  // representation)
  bool last4_nonzero = (addr[12] || addr[13] || addr[14] || addr[15]);
  if (last4_nonzero) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", addr[12], addr[13], addr[14],
                  addr[15]);
    return std::string(buf);
  }
  // Otherwise, try first 4 bytes (some locators may store v4 in first bytes)
  bool first4_nonzero = (addr[0] || addr[1] || addr[2] || addr[3]);
  if (first4_nonzero) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", addr[0], addr[1], addr[2],
                  addr[3]);
    return std::string(buf);
  }
  // Fallback: try to format as IPv6 hex groups if any non-zero byte present
  bool any_nonzero = false;
  for (int i = 0; i < 16; ++i)
    if (addr[i]) {
      any_nonzero = true;
      break;
    }
  if (any_nonzero) {
    // crude IPv6 formatting
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%"
                  "02x:%02x%02x",
                  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6],
                  addr[7], addr[8], addr[9], addr[10], addr[11], addr[12],
                  addr[13], addr[14], addr[15]);
    return std::string(buf);
  }
  return std::string();
}

class DdsTsnPublisherApp : public cSimpleModule {
 private:
  // 消息定时器
  cMessage* selfMsg = nullptr;   // 用于生成 DDS 样本
  cMessage* txPoller = nullptr;  // 用于轮询 Transport 队列

  // INET Socket
  UdpSocket socket;
  // Socket dedicated to DDS discovery/meta-traffic
  UdpSocket discoverySocket;
  L3Address destAddress;
  std::string destAddressStr;
  int destPort;
  int localPort = -1;

  // DDS 实体
  DomainParticipant* participant_ = nullptr;
  bool participant_owned_ = true;
  Publisher* ddsPublisher_ = nullptr;
  Topic* topic_ = nullptr;
  DataWriter* writer_ = nullptr;
  HelloWorldPubSubType typeSupport;

  // Transport 引用
  CustomTransport* transport_ = nullptr;
  std::shared_ptr<CustomTransportDescriptor> transportDesc_;

  // Topic -> TSN stream mapping (configurable via parameter `topicToStream`)
  std::unordered_map<std::string, std::string> topicToStreamMap_;
  void parseTopicToStream(const std::string& s);

  // Optional mapping: entity GUID (16-byte hex string) -> stream name
  std::unordered_map<std::string, std::string> entityToStreamMap_;
  void parseEntityToStream(const std::string& s);

  // Optional mapping: stream name -> VLAN id (used to request VLAN tags)
  std::unordered_map<std::string, int> streamToVlanMap_;
  void parseStreamToVlan(const std::string& s);

  // Cache: map from locator address bytes (16 bytes) to resolved L3Address to
  // avoid repeated string parsing and name resolution during runtime which can
  // significantly slow down simulation stepping.
  std::unordered_map<std::string, L3Address> locatorCache_;

  // Extract a writer GUID (12-byte prefix + 4-byte entityId) from RTPS packet
  // using a heuristic scan for the GUID prefix; returns 16-byte hex string
  static std::string extract_writer_guid_hex(const std::vector<uint8_t>& data);

  // 应用参数
  simtime_t startTime;
  simtime_t stopTime;
  simtime_t sendInterval;
  std::string messageStr;
  bool active = false;
  uint32_t seq = 0;
  // Optimization: thread-safe flag set by transport callback to notify of
  // outgoing data (kept for backward compatibility)
  std::atomic<bool> pendingOutgoing_{false};
  // poll interval for txPoller (s) - tunable to avoid hot polling
  simtime_t txPollInterval = 0.001;  // default 1ms

  // Discovery ports configuration (e.g. "7400-7450,8000")
  std::vector<std::pair<int, int>> discovery_port_ranges_;
  int default_discovery_port_ = 7400;
  std::string discovery_ports_str_;
  void parseDiscoveryPorts(const std::string& s);
  bool isDiscoveryPort(int port) const;
  // Simulation-to-DDS QoS scaling: factor to multiply relevant DDS timeouts
  // by (e.g. a factor of 10 makes heartbeats/leases 10x more lenient).
  double qosScaleFactor_ = 1.0;
  // Event-driven notifier removed: use atomic flag + periodic polling
  // (pendingOutgoing_)

 protected:
  virtual int numInitStages() const override { return NUM_INIT_STAGES; }
  virtual void initialize(int stage) override;
  virtual void handleMessage(cMessage* msg) override;
  virtual void finish() override;

  void processOutgoingPackets();  // 从 Transport 取出并发送到 Socket
};

Define_Module(DdsTsnPublisherApp);

void DdsTsnPublisherApp::initialize(int stage) {
  if (stage == INITSTAGE_LOCAL) {
    // 读取参数
    messageStr = par("message").stdstringValue();
    startTime = par("startTime");
    stopTime = par("stopTime");
    sendInterval = par("sendInterval");
    destPort = par("destPort");  //

    // discovery ports parameter (string like "7400-7450,8000")
    if (hasPar("discoveryPorts"))
      discovery_ports_str_ = par("discoveryPorts").stdstringValue();
    else
      discovery_ports_str_ = std::string("7400-7450");
    parseDiscoveryPorts(discovery_ports_str_);

    // 读取目标地址字符串（解析会在后续 init 阶段执行，以确保接口已注册）
    destAddressStr = par("destAddress").stdstringValue();

    selfMsg = new cMessage("dds-gen-sample");
    txPoller = new cMessage("dds-tx-poller");

    // 读取本地端口参数（实际 socket 输出门和 bind 在稍后的 init
    // 阶段设置，以避免协议注册竞争）
    localPort = par("localPort");
    // Read qosScaleFactor if provided (default 1.0 = no scaling)
    if (hasPar("qosScaleFactor")) qosScaleFactor_ = par("qosScaleFactor");
    if (qosScaleFactor_ > 1.0) {
      EV_INFO << "Publisher using qosScaleFactor=" << qosScaleFactor_
              << " (recommend increasing DDS heartbeat/lease durations)"
              << endl;
    }

    // 控制 txPoller 的轮询间隔（s），默认 1ms，避免过度轮询
    if (hasPar("txPollInterval")) txPollInterval = par("txPollInterval");
    // Notification pipe removed; rely on atomic flag + txPoller periodic check

  } else if (stage == INITSTAGE_APPLICATION_LAYER) {
    // 创建 Custom Transport Descriptor
    transportDesc_ = std::make_shared<CustomTransportDescriptor>();

    // Allow overriding the default transport max message size from NED/ini
    // parameter `maxMessageSize`. This helps constrain DDS message sizes so
    // that large messages aren't silently dropped at the link layer if INET's
    // fragmentation is not enabled.
    if (hasPar("maxMessageSize")) {
      int mm = par("maxMessageSize");
      if (mm > 0) {
        transportDesc_->max_message_size_ = static_cast<uint32_t>(mm);
        EV_INFO << "CustomTransport max_message_size set to " << mm
                << " bytes from parameter maxMessageSize" << endl;
      }
    }

    // Warn user when configured max message size exceeds typical Ethernet MTU
    if (transportDesc_->max_message_size_ > 1500) {
      EV_WARN << "CustomTransport::max_message_size ("
              << transportDesc_->max_message_size_
              << ") exceeds common Ethernet MTU (1500). Ensure INET's UDP "
                 "fragmentation is enabled or set a smaller maxMessageSize; "
                 "otherwise large DDS packets may be dropped by EthernetLink."
              << endl;
    }

    // 创建 DDS Participant
    DomainParticipantQos pqos;
    pqos.transport().use_builtin_transports = false;
    pqos.transport().user_transports.push_back(transportDesc_);

    // If qosScaleFactor is set, add it as a property (hint) and log the
    // recommendation. This does not automatically change Fast DDS timers but
    // helps document the intended behavior.
    if (qosScaleFactor_ > 1.0) {
      EV_WARN << "qosScaleFactor=" << qosScaleFactor_
              << " : recommended to increase DDS heartbeat/lease durations by "
                 "this factor to tolerate simulation slowdowns"
              << endl;
      try {
        pqos.properties().properties().push_back(
            eprosima::fastdds::rtps::Property("sim.qosScaleFactor",
                                              std::to_string(qosScaleFactor_)));
      } catch (...) {
      }
    }

    // 重要的 Discovery 配置：在静态仿真中，我们手动指定 Peer 或者简化 Discovery
    // 这里为了简单，我们暂时不做特殊 Discovery 配置，依靠 UDP 广播或单播连通性

    participant_ =
        DomainParticipantFactory::get_instance()->create_participant(0, pqos);
    if (!participant_)
      throw cRuntimeError("Failed to create DomainParticipant");

    // 获取刚刚创建的 Transport 实例
    transport_ = transportDesc_->get_created_transport();
    if (!transport_)
      throw cRuntimeError("Failed to retrieve CustomTransport instance");

    // 解析目标地址（现在接口表应该已经注册）
    if (!destAddressStr.empty()) {
      L3AddressResolver resolver;
      destAddress = resolver.resolve(destAddressStr.c_str());
    }

    // Parse topic->stream mapping parameter if provided (format:
    // "TopicA:streamA,TopicB:streamB")
    if (hasPar("topicToStream"))
      parseTopicToStream(par("topicToStream").stdstringValue());
    // Parse optional entity->stream mapping (GUID hex -> stream)
    if (hasPar("entityToStream"))
      parseEntityToStream(par("entityToStream").stdstringValue());
    // Parse optional stream->VLAN mapping (stream:VLAN)
    if (hasPar("streamToVlan"))
      parseStreamToVlan(par("streamToVlan").stdstringValue());

    // 注册类型、Topic、Writer (标准 DDS 流程)
    typeSupport.set_name("HelloWorld");
    participant_->register_type(TypeSupport(&typeSupport));
    ddsPublisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT);
    topic_ = participant_->create_topic("HelloWorldTopic", "HelloWorld",
                                        TOPIC_QOS_DEFAULT);
    writer_ = ddsPublisher_->create_datawriter(topic_, DATAWRITER_QOS_DEFAULT);

    // 设置 socket 输出门并绑定本地端口（此时协议注册与接口应已完成）
    socket.setOutputGate(gate("socketOut"));
    if (localPort != -1) {
      // Allow multiple sockets/apps on the same node to bind the same port
      socket.setReuseAddress(true);
      socket.bind(localPort);
    }

    // Bind a discovery socket so we can receive discovery/meta-traffic
    discoverySocket.setOutputGate(gate("socketOut"));
    // allow multiple sockets/apps on the same node to bind the discovery port
    discoverySocket.setReuseAddress(true);
    try {
      discoverySocket.bind(default_discovery_port_);
      // join RTPS multicast group
      L3Address maddr = L3AddressResolver().resolve("239.255.0.1");
      discoverySocket.joinMulticastGroup(maddr);
    } catch (...) {
      EV_WARN << "Failed to bind/join discovery socket; discovery packets may "
                 "be dropped"
              << endl;
    }

    // Register transport-level callback to notify when DDS enqueues outgoing
    // data. Use a lightweight, thread-safe atomic flag; the sim thread's
    // txPoller will check the flag and process outgoing packets.
    transport_->set_on_send_callback([this]() {
      // lightweight, thread-safe notification: set atomic flag; txPoller will
      // process
      pendingOutgoing_.store(true);
    });

    // 启动应用
    if (startTime < simTime())
      scheduleAt(simTime() + txPollInterval, selfMsg);
    else
      scheduleAt(startTime, selfMsg);

    // 启动传输层轮询 (使用可配置的间隔以避免热轮询，默认 1ms)
    scheduleAt(simTime() + txPollInterval, txPoller);
    active = true;
  }
}

void DdsTsnPublisherApp::handleMessage(cMessage* msg) {
  if (msg == txPoller) {
    // 1. 核心桥接逻辑：DDS -> Socket
    // Only process if transport signaled outgoing or do a periodic check
    if (pendingOutgoing_.load()) {
      pendingOutgoing_.store(false);
      processOutgoingPackets();
    }
    if (active) scheduleAt(simTime() + txPollInterval, txPoller);
  } else if (msg == selfMsg) {
    // 2. 生成业务数据 - only write when discovery/matching has occurred
    if (!active) return;

    int matched = 0;
    if (writer_) {
      std::vector<InstanceHandle_t> subs;
      ReturnCode_t rc = writer_->get_matched_subscriptions(subs);
      if (rc == RETCODE_OK) matched = static_cast<int>(subs.size());
    }

    if (matched > 0) {
      HelloWorld sample;
      sample.index(++seq);
      sample.message(messageStr);
      writer_->write(&sample);
      // EV_INFO << "DDS Written sample " << seq << " (matched=" << matched <<
      // ")" << endl;
    } else {
      EV_INFO << "Waiting for DDS discovery/matching (matched_subscriptions="
              << matched << ") - will retry" << endl;
    }

    if (stopTime < 0 || simTime() + sendInterval < stopTime)
      scheduleAt(simTime() + sendInterval, selfMsg);
  } else if (msg->getKind() == UDP_I_DATA) {
    // 3. 处理回包（例如 Discovery 确认包）
    // 如果是 Publisher，也需要接收 Discovery 数据才能匹配 Reader
    // 将数据包反向注入 Transport
    auto packet = check_and_cast<Packet*>(msg);
    auto chunk = packet->peekDataAsBytes();

    RawPacket rawPkt;
    rawPkt.data.resize(chunk->getChunkLength().get());
    chunk->copyToBuffer(rawPkt.data.data(), rawPkt.data.size());

    transport_->push_incoming(std::move(rawPkt));
    delete packet;
  } else {
    delete msg;
  }
}

void DdsTsnPublisherApp::processOutgoingPackets() {
  if (!transport_) return;

  RawPacket pkt;
  // 循环取出所有积压在 Transport 中的包
  while (transport_->pop_outgoing(pkt)) {
    // 封装为 INET Packet
    auto packet = new Packet("RTPS-Frame");
    auto dataChunk = makeShared<BytesChunk>(pkt.data);
    packet->insertAtBack(dataChunk);

    // If configured, attach a TSN stream label based on the Topic name of
    // this publisher. Consumers (bridging/queueing modules) can map the
    // stream label to PCP/queue selection. This is a safe, application-
    // level tagging approach that doesn't mutate RTPS payloads.
    // Attempt entity->stream mapping by heuristically extracting writer GUID
    std::string streamName;
    if (!entityToStreamMap_.empty()) {
      std::string guidHex = extract_writer_guid_hex(pkt.data);
      if (!guidHex.empty()) {
        auto it = entityToStreamMap_.find(guidHex);
        if (it != entityToStreamMap_.end()) streamName = it->second;
      }
    }

    // Fallback: topic->stream mapping if available
    if (streamName.empty() && topic_ && !topicToStreamMap_.empty()) {
      auto it = topicToStreamMap_.find(std::string(topic_->get_name()));
      if (it != topicToStreamMap_.end()) streamName = it->second;
    }

    if (!streamName.empty()) {
      auto labels = packet->addTag<inet::LabelsTag>();
      labels->appendLabels(streamName.c_str());
      EV_INFO << "Tagged outgoing RTPS packet for topic '"
              << (topic_ ? topic_->get_name() : "(unknown)")
              << "' with stream='" << streamName << "'" << endl;
      // If a VLAN mapping exists for this stream, attach a VlanReq tag so
      // lower-layer modules can insert 802.1Q headers and set PCP as needed.
      auto vIt = streamToVlanMap_.find(streamName);
      if (vIt != streamToVlanMap_.end()) {
        int vlanId = vIt->second;
        packet->addTagIfAbsent<inet::VlanReq>()->setVlanId(vlanId);
        EV_INFO << "Attached VlanReq vlanId=" << vlanId << " for stream='"
                << streamName << "'" << endl;
      }
    }

    // Determine target port and address using DDS-provided locator when
    // available. Preserve discovery ports (e.g., 7400-7450) so that PDP/EDP
    // discovery traffic is not remapped to application ports.
    int targetPort = destPort;
    L3Address targetAddr = destAddress;

    // If locator provides a port, use it
    if (pkt.remote.port != 0) {
      targetPort = static_cast<int>(pkt.remote.port);
    }

    // Try to extract an IP from locator and resolve it to L3Address when
    // present. Use a small cache keyed by the raw 16-byte address so we avoid
    // repeated string parsing and name resolution (which can significantly
    // slow down simulation stepping when done very frequently).
    std::string addrKey(reinterpret_cast<const char*>(pkt.remote.address), 16);
    // If the address is all-zero, locator contains no address info
    if (addrKey.find_first_not_of('\0') != std::string::npos) {
      auto it = locatorCache_.find(addrKey);
      if (it != locatorCache_.end()) {
        targetAddr = it->second;
      } else {
        std::string ip = locator_to_ipstring(pkt.remote);
        if (!ip.empty()) {
          try {
            L3AddressResolver resolver;
            L3Address resolved = resolver.resolve(ip.c_str());
            // Cache the resolved L3Address for this locator address bytes
            locatorCache_.emplace(addrKey, resolved);
            targetAddr = resolved;
          } catch (...) {
            // resolution failed; keep configured destAddress
          }
        }
      }
    }

    // Heuristic: if this looks like RTPS traffic and no locator port was set,
    // treat it as discovery/meta-traffic and send to the discovery range's
    // default port to preserve discovery behavior.
    bool isDiscovery = false;
    if (pkt.remote.port != 0 &&
        isDiscoveryPort(static_cast<int>(pkt.remote.port))) {
      isDiscovery = true;
    } else if (pkt.remote.port == 0 && pkt.data.size() >= 4 &&
               pkt.data[0] == 'R' && pkt.data[1] == 'T' && pkt.data[2] == 'P' &&
               pkt.data[3] == 'S') {
      isDiscovery = true;
    }

    if (isDiscovery) {
      if (pkt.remote.port != 0) {
        // port already set from locator and is in discovery range
        targetPort = static_cast<int>(pkt.remote.port);
      } else {
        // locator port not present; fall back to configured discovery default
        targetPort = default_discovery_port_;
      }
      EV_INFO << "Detected discovery RTPS packet; preserving discovery port "
              << targetPort << endl;
    }

    socket.sendTo(packet, targetAddr, targetPort);

    EV_INFO << "Bridge sent " << pkt.data.size() << " bytes to " << targetAddr
            << ":" << targetPort << (isDiscovery ? " (discovery)" : "") << endl;
  }
}

void DdsTsnPublisherApp::finish() {
  active = false;
  if (selfMsg) cancelAndDelete(selfMsg);
  if (txPoller) cancelAndDelete(txPoller);

  // Clean up DDS entities only if we created them
  if (ddsPublisher_ && writer_) {
    ddsPublisher_->delete_datawriter(writer_);
    writer_ = nullptr;
  }

  if (participant_owned_) {
    if (ddsPublisher_) {
      participant_->delete_publisher(ddsPublisher_);
      ddsPublisher_ = nullptr;
    }
    if (topic_) {
      participant_->delete_topic(topic_);
      topic_ = nullptr;
    }
    if (participant_) {
      eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
          ->delete_participant(participant_);
      participant_ = nullptr;
    }
  } else {
    // If we didn't own the participant, only clean up entities we created
    if (ddsPublisher_) {
      participant_->delete_publisher(ddsPublisher_);
      ddsPublisher_ = nullptr;
    }
    if (topic_) {
      participant_->delete_topic(topic_);
      topic_ = nullptr;
    }
    // Do not delete participant_ when not owned
  }

  delete selfMsg;
  selfMsg = nullptr;
}

// Parse discovery port parameter string (e.g. "7400-7450,8000") into ranges
void DdsTsnPublisherApp::parseDiscoveryPorts(const std::string& s) {
  discovery_port_ranges_.clear();
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    // trim whitespace (avoid complex template deduction by using simple loops)
    size_t start = 0;
    while (start < token.size() &&
           std::isspace(static_cast<unsigned char>(token[start])))
      ++start;
    if (start > 0) token.erase(0, start);
    size_t endpos = token.size();
    while (endpos > 0 &&
           std::isspace(static_cast<unsigned char>(token[endpos - 1])))
      --endpos;
    if (endpos < token.size()) token.erase(endpos);

    if (token.empty()) continue;
    auto dash = token.find('-');
    try {
      if (dash == std::string::npos) {
        int p = std::stoi(token);
        discovery_port_ranges_.push_back({p, p});
      } else {
        int a = std::stoi(token.substr(0, dash));
        int b = std::stoi(token.substr(dash + 1));
        if (a > b) std::swap(a, b);
        discovery_port_ranges_.push_back({a, b});
      }
    } catch (...) {
      EV_WARN << "Invalid discovery port token '" << token
              << "' in discoveryPorts parameter; ignoring" << endl;
    }
  }
  if (discovery_port_ranges_.empty()) {
    discovery_port_ranges_.push_back({7400, 7400});
  }
  default_discovery_port_ = discovery_port_ranges_[0].first;
}

// Implementation: extract_writer_guid_hex
std::string DdsTsnPublisherApp::extract_writer_guid_hex(
    const std::vector<uint8_t>& data) {
  if (data.size() < 20) return std::string();
  const uint8_t* p = data.data();
  const uint8_t* prefix = p + 8;  // GUID prefix offset in RTPS header
  size_t sz = data.size();
  for (size_t i = 20; i + 16 <= sz; ++i) {
    if (std::memcmp(prefix, p + i, 12) == 0) {
      const uint8_t* entity = p + i + 12;
      return bytes_to_hex(prefix, 12) + bytes_to_hex(entity, 4);
    }
  }
  return std::string();
}

// Parse mapping from topic names to TSN stream names of the form
// "TopicA:streamA,TopicB:streamB". Values are stored in
// topicToStreamMap_ used to label outgoing packets.
void DdsTsnPublisherApp::parseTopicToStream(const std::string& s) {
  topicToStreamMap_.clear();
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    size_t colon = token.find(':');
    if (colon == std::string::npos) continue;
    std::string topic = token.substr(0, colon);
    std::string stream = token.substr(colon + 1);
    // trim
    auto trim = [](std::string& x) {
      size_t i = 0;
      while (i < x.size() && std::isspace((unsigned char)x[i])) ++i;
      if (i) x.erase(0, i);
      size_t j = x.size();
      while (j > 0 && std::isspace((unsigned char)x[j - 1])) --j;
      if (j < x.size()) x.erase(j);
    };
    trim(topic);
    trim(stream);
    if (!topic.empty() && !stream.empty()) topicToStreamMap_[topic] = stream;
  }
}

// Parse mapping from 16-byte GUID hex string to stream name, format:
// "0123abcd...:streamA,deadbeef...:streamB"
void DdsTsnPublisherApp::parseEntityToStream(const std::string& s) {
  entityToStreamMap_.clear();
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    size_t colon = token.find(':');
    if (colon == std::string::npos) continue;
    std::string guid = token.substr(0, colon);
    std::string stream = token.substr(colon + 1);
    auto trim = [](std::string& x) {
      size_t i = 0;
      while (i < x.size() && std::isspace((unsigned char)x[i])) ++i;
      if (i) x.erase(0, i);
      size_t j = x.size();
      while (j > 0 && std::isspace((unsigned char)x[j - 1])) --j;
      if (j < x.size()) x.erase(j);
    };
    trim(guid);
    trim(stream);
    if (!guid.empty() && !stream.empty()) entityToStreamMap_[guid] = stream;
  }
}

// Parse mapping from stream name to VLAN id, format: "video:100,disc:10"
void DdsTsnPublisherApp::parseStreamToVlan(const std::string& s) {
  streamToVlanMap_.clear();
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    size_t colon = token.find(':');
    if (colon == std::string::npos) continue;
    std::string stream = token.substr(0, colon);
    std::string vlanStr = token.substr(colon + 1);
    auto trim = [](std::string& x) {
      size_t i = 0;
      while (i < x.size() && std::isspace((unsigned char)x[i])) ++i;
      if (i) x.erase(0, i);
      size_t j = x.size();
      while (j > 0 && std::isspace((unsigned char)x[j - 1])) --j;
      if (j < x.size()) x.erase(j);
    };
    trim(stream);
    trim(vlanStr);
    if (!stream.empty() && !vlanStr.empty()) {
      try {
        int v = std::stoi(vlanStr);
        streamToVlanMap_[stream] = v;
      } catch (...) {
      }
    }
  }
}

bool DdsTsnPublisherApp::isDiscoveryPort(int port) const {
  for (auto& r : discovery_port_ranges_) {
    if (port >= r.first && port <= r.second) return true;
  }
  return false;
}