#include <omnetpp.h>

#include <string>
#include <vector>

#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/rtps/common/SerializedPayload.hpp>

#include "HelloWorldCdrAux.hpp"
#include "HelloWorldPubSubTypes.hpp"

#include "inet/common/InitStages.h"
#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/chunk/Chunk.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"

using namespace omnetpp;
using namespace inet;
using namespace eprosima::fastdds::dds;
using namespace eprosima::fastdds::rtps;

class DdsTsnPublisherApp : public cSimpleModule, public UdpSocket::ICallback {
  private:
    UdpSocket socket;
    cMessage *selfMsg = nullptr;
    L3Address destAddress;
    std::string destAddressStr;
    int destPort = -1;
    int localPort = -1;
    simtime_t startTime = SIMTIME_ZERO;
    simtime_t stopTime = -1;
    simtime_t sendInterval = 1.0;
    std::string message;
    bool active = false;
    uint32_t seq = 0;
    HelloWorldPubSubType typeSupport;

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;

    void startApp();
    void stopApp();
    void scheduleNextSend(simtime_t delay);
    void sendSample();

    // UdpSocket::ICallback overrides
    virtual void socketDataArrived(UdpSocket *socket, Packet *packet) override;
    virtual void socketErrorArrived(UdpSocket *socket, Indication *indication) override;
    virtual void socketClosed(UdpSocket *socket) override;
};

Define_Module(DdsTsnPublisherApp);

void DdsTsnPublisherApp::initialize(int stage) {
  cSimpleModule::initialize(stage);

  if (stage == INITSTAGE_LOCAL) {
    localPort = par("localPort");
    destPort = par("destPort");
    message = par("message").stdstringValue();
    startTime = par("startTime");
    stopTime = par("stopTime");
    sendInterval = par("sendInterval");
    destAddressStr = par("destAddress").stdstringValue();

    selfMsg = new cMessage("dds-tsn-send");
    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);
  } else if (stage == INITSTAGE_APPLICATION_LAYER) {
    destAddress = L3AddressResolver().resolve(destAddressStr.c_str());
    if (destAddress.isUnspecified()) {
      throw cRuntimeError("Failed to resolve destination address '%s'", destAddressStr.c_str());
    }
    if (localPort >= 0) {
      socket.bind(localPort);
    }
    startApp();
  }
}

void DdsTsnPublisherApp::startApp() {
  active = true;
  if (startTime < simTime()) {
    scheduleNextSend(SIMTIME_ZERO);
  } else {
    scheduleNextSend(startTime - simTime());
  }
}

void DdsTsnPublisherApp::stopApp() {
  active = false;
  if (selfMsg->isScheduled()) cancelEvent(selfMsg);
  socket.close();
}

void DdsTsnPublisherApp::scheduleNextSend(simtime_t delay) {
  if (!active) return;
  if (stopTime >= SIMTIME_ZERO && simTime() + delay > stopTime) return;
  scheduleAt(simTime() + delay, selfMsg);
}

void DdsTsnPublisherApp::sendSample() {
  HelloWorld sample;
  sample.index(++seq);
  sample.message(message.c_str());

  SerializedPayload_t payload(HelloWorld_max_cdr_typesize + 4);
  payload.length = 0;

  if (!typeSupport.serialize(&sample, payload, DataRepresentationId_t::XCDR_DATA_REPRESENTATION)) {
    throw cRuntimeError("Failed to serialize HelloWorld sample");
  }

  std::vector<uint8_t> serialized(payload.data, payload.data + payload.length);
  auto chunk = makeShared<BytesChunk>(serialized);

  auto packet = new Packet("HelloWorldSample");
  packet->addTagIfAbsent<CreationTimeTag>()->setCreationTime(simTime());
  packet->insertAtBack(chunk);

  socket.sendTo(packet, destAddress, destPort);
  EV_INFO << "[DDS TSN PUB] Sent sample seq=" << sample.index()
          << " len=" << payload.length << "B to " << destAddress << ":"
          << destPort << "\n";
}

void DdsTsnPublisherApp::handleMessage(cMessage *msg) {
  if (msg->isSelfMessage()) {
    if (active) {
      sendSample();
      scheduleNextSend(sendInterval);
    }
  } else {
    socket.processMessage(msg);
  }
}

void DdsTsnPublisherApp::finish() {
  stopApp();
  delete selfMsg;
  selfMsg = nullptr;
}

void DdsTsnPublisherApp::socketDataArrived(UdpSocket *socket, Packet *packet) {
  EV_WARN << "[DDS TSN PUB] Unexpected data arrived, discarding" << endl;
  delete packet;
}

void DdsTsnPublisherApp::socketErrorArrived(UdpSocket *socket, Indication *indication) {
  EV_WARN << "[DDS TSN PUB] Socket error: " << indication->getName() << endl;
  delete indication;
}

void DdsTsnPublisherApp::socketClosed(UdpSocket *socket) {
  // nothing
}
