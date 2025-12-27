#include <omnetpp.h>

#include <atomic>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <thread>

#include "CustomTransport.hpp"
#include "CustomTransportDescriptor.hpp"
#include "HelloWorldPubSubTypes.hpp"
#include "inet/common/InitStages.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"

using namespace omnetpp;
using namespace inet;
using namespace eprosima::fastdds::dds;

// unique message kind for our poll timer to avoid collisions
static const int MSGKIND_SUB_POLL = 12345;

class DDSSubscriberListener
    : public eprosima::fastdds::dds::DataReaderListener {
 public:
  DDSSubscriberListener(std::atomic<bool>* pending) : pending_(pending) {}
  void on_data_available(
      eprosima::fastdds::dds::DataReader* /*reader*/) override {
    if (pending_) pending_->store(true);
  }

 private:
  std::atomic<bool>* pending_;
};

class DdsTsnSubscriberApp : public cSimpleModule {
 private:
  cMessage* pollTimer_ = nullptr;
  simtime_t pollInterval_;

  // INET Socket
  UdpSocket socket;
  int localPort_ = -1;

  // Event-driven notifier removed: use atomic flag + periodic polling
  // (pendingIncoming_)
  std::atomic<bool> pendingIncoming_{false};
  eprosima::fastdds::dds::DataReaderListener* readerListener_ = nullptr;

  // Simulation-to-DDS QoS scaling hint
  double qosScaleFactor_ = 1.0;

  // DDS
  DomainParticipant* participant_ = nullptr;
  bool participant_owned_ = true;
  Subscriber* ddsSubscriber_ = nullptr;
  DataReader* reader_ = nullptr;
  Topic* topic_ = nullptr;
  HelloWorldPubSubType typeSupport_;

  // Transport
  CustomTransport* transport_ = nullptr;
  std::shared_ptr<CustomTransportDescriptor> transportDesc_;

  // 统计
  int samplesReceived = 0;

 protected:
  virtual int numInitStages() const override { return NUM_INIT_STAGES; }
  virtual void initialize(int stage) override;
  virtual void handleMessage(cMessage* msg) override;
  virtual void finish() override;
};

Define_Module(DdsTsnSubscriberApp);

void DdsTsnSubscriberApp::initialize(int stage) {
  if (stage == INITSTAGE_LOCAL) {
    pollInterval_ = par("pollInterval");  // [cite: 7]
    pollTimer_ = new cMessage("dds-sub-poll");
    // ensure a unique message kind to avoid kind collisions
    pollTimer_->setKind(MSGKIND_SUB_POLL);

    // Using periodic poll timer + atomic flag to detect incoming samples

    // 读取本地端口参数（稍后在 APPLICATION_LAYER 设置 socket 输出门并 bind）
    localPort_ = par("localPort");  // [cite: 6]

    // Read qosScaleFactor if provided (default 1.0 = no scaling)
    if (hasPar("qosScaleFactor")) qosScaleFactor_ = par("qosScaleFactor");
    if (qosScaleFactor_ > 1.0) {
      EV_INFO << "Subscriber using qosScaleFactor=" << qosScaleFactor_
              << " (recommend increasing DDS heartbeat/lease durations)"
              << endl;
    }
    // Notification pipe removed; rely on atomic flag + periodic poll timer

  } else if (stage == INITSTAGE_APPLICATION_LAYER) {
    // 创建 Custom Transport
    transportDesc_ = std::make_shared<CustomTransportDescriptor>();
    DomainParticipantQos pqos;
    pqos.transport().use_builtin_transports = false;
    pqos.transport().user_transports.push_back(transportDesc_);
    // If a qosScaleFactor parameter was provided, attach a property to the
    // participant QoS as a hint (for external inspection or future Fast DDS
    // hooks). We also log recommended QoS adjustments.
    if (qosScaleFactor_ > 1.0) {
      EV_WARN << "qosScaleFactor=" << qosScaleFactor_
              << " : recommended to increase DDS heartbeat/lease durations by "
                 "this factor to tolerate simulation slowdowns"
              << endl;
      try {
        // Use PropertyPolicy if available (best-effort; may be no-op)
        pqos.properties().properties().push_back(
            eprosima::fastdds::rtps::Property("sim.qosScaleFactor",
                                              std::to_string(qosScaleFactor_)));
      } catch (...) {
        // If Property API differs in this Fast DDS version, ignore and rely
        // on logs/documentation.
      }
    }
    participant_ =
        DomainParticipantFactory::get_instance()->create_participant(0, pqos);
    transport_ = transportDesc_->get_created_transport();

    // DDS Setup
    typeSupport_.set_name("HelloWorld");
    participant_->register_type(TypeSupport(&typeSupport_));
    ddsSubscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
    topic_ = participant_->create_topic("HelloWorldTopic", "HelloWorld",
                                        TOPIC_QOS_DEFAULT);

    // Create and register DataReader listener that sets an atomic flag when
    // data is available (no external watcher thread)
    readerListener_ = new DDSSubscriberListener(&pendingIncoming_);
    reader_ = ddsSubscriber_->create_datareader(topic_, DATAREADER_QOS_DEFAULT,
                                                readerListener_);

    // 设置 socket 输出门并绑定本地端口（此时协议注册与接口应已完成）
    socket.setOutputGate(gate("socketOut"));
    if (localPort_ != -1) socket.bind(localPort_);

    // Use periodic poll timer to check for new DDS samples driven by the
    // atomic pendingIncoming_ flag
    scheduleAt(simTime() + pollInterval_, pollTimer_);
  }
}

void DdsTsnSubscriberApp::handleMessage(cMessage* msg) {
  if (msg == pollTimer_) {
    // Check atomic flag set by DDS listener and process only when set
    if (reader_ && pendingIncoming_.exchange(false)) {
      HelloWorld sample;
      SampleInfo info;
      while (reader_->take_next_sample(&sample, &info) == RETCODE_OK) {
        if (info.valid_data) {
          samplesReceived++;
          EV_INFO << "[DDS-SUB] Received Seq=" << sample.index()
                  << " Msg=" << sample.message() << endl;
        }
      }
    }
    scheduleAt(simTime() + pollInterval_, pollTimer_);
  } else if (msg->getKind() == UDP_I_DATA) {
    // 2. 网络层收到数据 -> 注入 DDS Transport
    // Ensure this message is actually a Packet (some timers might have kinds
    // that overlap with UDP_I_DATA and cause miscasts otherwise).
    auto packet = dynamic_cast<Packet*>(msg);
    if (!packet) {
      EV_WARN << "Received non-packet with UDP_I_DATA kind; ignoring" << endl;
      delete msg;
      return;
    }
    auto chunk = packet->peekDataAsBytes();

    RawPacket rawPkt;
    rawPkt.data.resize(chunk->getChunkLength().get());
    chunk->copyToBuffer(rawPkt.data.data(), rawPkt.data.size());

    // 记录来源信息（可选，如果需要更精确的 DDS Locator 映射）
    // rawPkt.remote.port = ...

    // push_incoming 会同步调用 reader 的 OnDataReceived (如果 Fast DDS
    // 配置为同步) 或者通知 Fast DDS 线程（如果配置为异步）。
    // 无论如何，数据进入了 DDS 栈。
    if (transport_) {
      transport_->push_incoming(std::move(rawPkt));
    }
    delete packet;
  } else {
    delete msg;
  }
}

void DdsTsnSubscriberApp::finish() {
  // stop timer
  if (pollTimer_) {
    cancelAndDelete(pollTimer_);
    pollTimer_ = nullptr;
  }

  if (readerListener_) {
    delete readerListener_;
    readerListener_ = nullptr;
  }

  if (participant_) {
    if (ddsSubscriber_) {
      if (reader_) ddsSubscriber_->delete_datareader(reader_);
      participant_->delete_subscriber(ddsSubscriber_);
      ddsSubscriber_ = nullptr;
    }
    if (topic_) {
      participant_->delete_topic(topic_);
      topic_ = nullptr;
    }

    if (participant_owned_) {
      DomainParticipantFactory::get_instance()->delete_participant(
          participant_);
      participant_ = nullptr;
    }
  }
}