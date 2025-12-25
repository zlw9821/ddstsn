#include <omnetpp.h>

#include <vector>

#include <fastdds/rtps/common/SerializedPayload.hpp>

#include "HelloWorldPubSubTypes.hpp"

#include "inet/common/InitStages.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/common/TimeTag_m.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"

using namespace omnetpp;
using namespace inet;
using namespace eprosima::fastdds::rtps;

class DdsTsnSubscriberApp : public cSimpleModule, public UdpSocket::ICallback {
  private:
    UdpSocket socket;
    int localPort = -1;
    HelloWorldPubSubType typeSupport;

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;

    // UdpSocket::ICallback overrides
    virtual void socketDataArrived(UdpSocket *socket, Packet *packet) override;
    virtual void socketErrorArrived(UdpSocket *socket, Indication *indication) override;
    virtual void socketClosed(UdpSocket *socket) override;
};

Define_Module(DdsTsnSubscriberApp);

void DdsTsnSubscriberApp::initialize(int stage) {
  cSimpleModule::initialize(stage);

  if (stage == INITSTAGE_LOCAL) {
    localPort = par("localPort");
    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);
  } else if (stage == INITSTAGE_APPLICATION_LAYER) {
    socket.bind(localPort);
  }
}

void DdsTsnSubscriberApp::handleMessage(cMessage *msg) {
  socket.processMessage(msg);
}

void DdsTsnSubscriberApp::finish() {
  socket.close();
}

void DdsTsnSubscriberApp::socketDataArrived(UdpSocket *socket, Packet *packet) {
  auto bytes = packet->peekDataAsBytes();
  const auto bitLength = bytes->getChunkLength();
  if (bitLength.get() % 8 != 0) {
    EV_WARN << "[DDS TSN SUB] Dropping non byte-aligned payload of length=" << bitLength << endl;
    delete packet;
    return;
  }
  const size_t len = bitLength.get() / 8;
  if (len == 0) {
    EV_WARN << "[DDS TSN SUB] Dropping empty payload" << endl;
    delete packet;
    return;
  }

  SerializedPayload_t payload(len);
  bytes->copyToBuffer(payload.data, len);
  payload.length = len;

  HelloWorld sample;
  if (typeSupport.deserialize(payload, &sample)) {
    simtime_t latency = SIMTIME_ZERO;
    if (auto creationTag = packet->findTag<CreationTimeTag>()) {
      latency = simTime() - creationTag->getCreationTime();
    }
    EV_INFO << "[DDS TSN SUB] Seq=" << sample.index()
            << " msg=\"" << sample.message() << "\""
            << " latency=" << latency << "\n";
  } else {
    EV_WARN << "[DDS TSN SUB] Failed to deserialize payload of len=" << len << endl;
  }

  delete packet;
}

void DdsTsnSubscriberApp::socketErrorArrived(UdpSocket *socket, Indication *indication) {
  EV_WARN << "[DDS TSN SUB] Socket error: " << indication->getName() << endl;
  delete indication;
}

void DdsTsnSubscriberApp::socketClosed(UdpSocket *socket) {
  // nothing
}
