// src/DdsTsnPublisherApp.cpp
#include <omnetpp.h>

#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/rtps/common/SerializedPayload.hpp>
#include <string>
#include <vector>

#include "CustomTransport.hpp"
#include "CustomTransportDescriptor.hpp"
#include "HelloWorldCdrAux.hpp"
#include "HelloWorldPubSubTypes.hpp"
#include "inet/common/InitStages.h"
#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/common/packet/chunk/Chunk.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"

using namespace omnetpp;
using namespace inet;
using namespace eprosima::fastdds::dds;
using namespace eprosima::fastdds::rtps;

class DdsTsnPublisherApp : public cSimpleModule {
 private:
  cMessage* selfMsg = nullptr;

  // DDS entities
  eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
  eprosima::fastdds::dds::Publisher* ddsPublisher_ = nullptr;
  eprosima::fastdds::dds::Topic* topic_ = nullptr;
  eprosima::fastdds::dds::DataWriter* writer_ = nullptr;

  // If we create the participant ourselves, we must delete it on finish
  bool participant_owned_ = false;

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
  virtual void handleMessage(cMessage* msg) override;
  virtual void finish() override;

  void startApp();
  void stopApp();
  void scheduleNextSend(simtime_t delay);
  void sendSample();
};

Define_Module(DdsTsnPublisherApp);

void DdsTsnPublisherApp::initialize(int stage) {
  cSimpleModule::initialize(stage);

  if (stage == INITSTAGE_LOCAL) {
    message = par("message").stdstringValue();
    startTime = par("startTime");
    stopTime = par("stopTime");
    sendInterval = par("sendInterval");

    selfMsg = new cMessage("dds-tsn-send");
  } else if (stage == INITSTAGE_APPLICATION_LAYER) {
    // Try to reuse a participant created by the bridge (if any)
    participant_ =
        eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
            ->lookup_participant(0);

    if (!participant_) {
      // No existing participant found; create one and inject our custom
      // transport
      DomainParticipantQos pqos;
      pqos.transport().use_builtin_transports = false;
      auto desc = std::make_shared<CustomTransportDescriptor>();
      pqos.transport().user_transports.push_back(desc);

      participant_ =
          DomainParticipantFactory::get_instance()->create_participant(0, pqos);
      if (!participant_) {
        throw cRuntimeError(
            "Failed to create DomainParticipant for DDS TSN Publisher");
      }
      participant_owned_ = true;
      EV_INFO << "[DDS TSN PUB] Participant ready: " << participant_ << "\n";
    } else {
      EV_INFO << "[DDS TSN PUB] Reusing existing participant: " << participant_
              << "\n";
    }

    // Register type and create publisher/datawriter as a normal DDS app would
    typeSupport.set_name("HelloWorld");
    participant_->register_type(TypeSupport(&typeSupport));
    EV_INFO << "[DDS TSN PUB] Registered type HelloWorld\n";

    ddsPublisher_ = participant_->create_publisher(PUBLISHER_QOS_DEFAULT);
    if (!ddsPublisher_) throw cRuntimeError("Failed to create Publisher");
    EV_INFO << "[DDS TSN PUB] Publisher created: " << ddsPublisher_ << "\n";

    // Try to find an existing Topic first to avoid duplicate-topic errors
    bool created_topic = false;
    topic_ = participant_->find_topic("HelloWorldTopic", Duration_t{0, 0});
    if (!topic_) {
      topic_ = participant_->create_topic("HelloWorldTopic", "HelloWorld",
                                          TOPIC_QOS_DEFAULT);
      if (topic_) {
        created_topic = true;
      } else {
        // Fallback: wait shortly for a topic created concurrently by another
        // module
        topic_ = participant_->find_topic("HelloWorldTopic", Duration_t{1, 0});
      }
    }
    if (!topic_)
      throw cRuntimeError("Failed to create or find Topic 'HelloWorldTopic'");

    if (created_topic)
      EV_INFO << "[DDS TSN PUB] Created Topic 'HelloWorldTopic'\n";
    else
      EV_INFO << "[DDS TSN PUB] Using existing Topic 'HelloWorldTopic'\n";

    EV_INFO << "[DDS TSN PUB] Creating DataWriter for topic " << topic_ << "\n";
    writer_ = ddsPublisher_->create_datawriter(topic_, DATAWRITER_QOS_DEFAULT);
    if (!writer_) throw cRuntimeError("Failed to create DataWriter");
    EV_INFO << "[DDS TSN PUB] DataWriter created: " << writer_ << "\n";

    writer_ = ddsPublisher_->create_datawriter(topic_, DATAWRITER_QOS_DEFAULT);
    if (!writer_) throw cRuntimeError("Failed to create DataWriter");

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
}

void DdsTsnPublisherApp::scheduleNextSend(simtime_t delay) {
  if (!active) return;
  if (stopTime >= SIMTIME_ZERO && simTime() + delay > stopTime) return;
  scheduleAt(simTime() + delay, selfMsg);
}

void DdsTsnPublisherApp::sendSample() {
  if (!writer_) {
    EV_ERROR << "DataWriter not initialized, cannot send sample\n";
    return;
  }

  HelloWorld sample;
  sample.index(++seq);
  sample.message(message.c_str());

  if (writer_->write(&sample) != 0) {
    EV_ERROR << "Failed to write HelloWorld sample via DDS\n";
  } else {
    EV_INFO << "[DDS TSN PUB] Published sample seq=" << sample.index() << "\n";
  }
}

void DdsTsnPublisherApp::handleMessage(cMessage* msg) {
  if (msg->isSelfMessage()) {
    if (active) {
      sendSample();
      scheduleNextSend(sendInterval);
    }
  } else {
    // no sockets used by this module; ignore external messages
    delete msg;
  }
}

void DdsTsnPublisherApp::finish() {
  stopApp();

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
