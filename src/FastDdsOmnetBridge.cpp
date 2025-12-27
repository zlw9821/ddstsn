#include <omnetpp.h>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>

#include "CustomTransport.hpp"
#include "CustomTransportDescriptor.hpp"
#include "inet/common/InitStages.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"

using namespace omnetpp;
using namespace inet;
using namespace eprosima::fastdds::dds;
using namespace eprosima::fastdds::rtps;

class FastDdsOmnetBridge : public cSimpleModule, public UdpSocket::ICallback {
 private:
  UdpSocket socket_;
  cMessage* pollTimer_ = nullptr;

  // Fast-DDS
  DomainParticipant* participant_ = nullptr;
  std::shared_ptr<CustomTransportDescriptor> desc_;
  CustomTransport* transport_ = nullptr;

  int localPort_ = -1;
  std::string peerAddress_;
  int peerPort_ = -1;
  simtime_t pollInterval_;

 protected:
  int numInitStages() const override { return NUM_INIT_STAGES; }
  void initialize(int stage) override;
  void handleMessage(cMessage* msg) override;
  void finish() override;

  // UdpSocket callbacks
  void socketDataArrived(UdpSocket*, Packet* packet) override;
  void socketErrorArrived(UdpSocket*, Indication* ind) override { delete ind; }
  void socketClosed(UdpSocket*) override {}
};

Define_Module(FastDdsOmnetBridge);

void FastDdsOmnetBridge::initialize(int stage) {
  if (stage == INITSTAGE_LOCAL) {
    localPort_ = par("localPort");
    peerAddress_ = par("peerAddress").stdstringValue();
    peerPort_ = par("peerPort");
    pollInterval_ = par("pollInterval");

    pollTimer_ = new cMessage("bridgePoll");

    socket_.setOutputGate(gate("socketOut"));
    socket_.setCallback(this);
  } else if (stage == INITSTAGE_APPLICATION_LAYER) {
    socket_.bind(localPort_);

    // ---------- 创建 Fast-DDS Participant 并注入自定义 transport ----------
    DomainParticipantQos pqos;
    pqos.transport().use_builtin_transports = false;

    desc_ = std::make_shared<CustomTransportDescriptor>();
    pqos.transport().user_transports.push_back(desc_);

    participant_ =
        DomainParticipantFactory::get_instance()->create_participant(0, pqos);
    if (!participant_) {
      throw cRuntimeError("Failed to create DomainParticipant");
    }

    // 尝试从我们提供的 descriptor 获取刚创建的 CustomTransport 实例
    transport_ = desc_->get_created_transport();
    if (!transport_) {
      throw cRuntimeError(
          "CustomTransport not found (descriptor did not create transport)");
    }

    scheduleAt(simTime() + pollInterval_, pollTimer_);
  }
}

void FastDdsOmnetBridge::handleMessage(cMessage* msg) {
  if (msg == pollTimer_) {
    // 轮询 outgoing queue
    RawPacket pkt;
    while (transport_->pop_outgoing(pkt)) {
      auto bytes = makeShared<BytesChunk>(pkt.data);
      auto packet = new Packet("rtps");
      packet->insertAtBack(bytes);

      auto dest = L3AddressResolver().resolve(peerAddress_.c_str());
      socket_.sendTo(packet, dest, peerPort_);
    }

    scheduleAt(simTime() + pollInterval_, pollTimer_);
  } else {
    socket_.processMessage(msg);
  }
}

void FastDdsOmnetBridge::socketDataArrived(UdpSocket*, Packet* packet) {
  auto bytes = packet->peekDataAsBytes();
  size_t len = bytes->getChunkLength().get() / 8;

  // 检查是否成功获取了 transport_
  if (!transport_) {
    EV_ERROR << "CustomTransport not initialized, dropping packet\n";
    delete packet;
    return;
  }

  RawPacket pkt;
  pkt.data.resize(len);
  bytes->copyToBuffer(pkt.data.data(), len);

  transport_->push_incoming(std::move(pkt));
  delete packet;
}

void FastDdsOmnetBridge::finish() {
  cancelAndDelete(pollTimer_);
  if (participant_)
    DomainParticipantFactory::get_instance()->delete_participant(participant_);
}
