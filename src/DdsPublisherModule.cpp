// src/DdsPublisherModule.cpp
#include <omnetpp.h>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>

#include "HelloWorldPubSubTypes.hpp"

using namespace omnetpp;
using namespace eprosima::fastdds::dds;

class DdsPublisherModule : public cSimpleModule {
 private:
  cMessage* timer = nullptr;

  DomainParticipant* participant = nullptr;
  eprosima::fastdds::dds::Publisher* ddsPublisher = nullptr;
  Topic* topic = nullptr;
  DataWriter* writer = nullptr;

  HelloWorldPubSubType type;
  uint32_t seq = 0;

 protected:
  virtual void initialize() override;
  virtual void handleMessage(cMessage* msg) override;
  virtual void finish() override;
  virtual ~DdsPublisherModule();
};

Define_Module(DdsPublisherModule);

//------------------------------------------------------------

void DdsPublisherModule::initialize() {
  timer = new cMessage("pubtimer");
  scheduleAt(simTime() + 1.0, timer);

  DomainParticipantQos pqos;
  participant =
      DomainParticipantFactory::get_instance()->create_participant(0, pqos);
  if (!participant) throw cRuntimeError("Failed to create Participant");

  /* 1. 修正：使用 set_name 并通过 TypeSupport 注册 */
  type.set_name("HelloWorld");
  participant->register_type(TypeSupport(&type));

  ddsPublisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
  if (!ddsPublisher) throw cRuntimeError("Failed to create Publisher");

  /* 使用注册时的名称 "HelloWorld" */
  topic = participant->create_topic("HelloWorldTopic", "HelloWorld",
                                    TOPIC_QOS_DEFAULT);

  writer = ddsPublisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT);
}

DdsPublisherModule::~DdsPublisherModule() {
  cancelAndDelete(timer);

  if (participant) {
    /* 2. 修正：通过 ddsPublisher 删除 writer */
    if (ddsPublisher && writer) {
      ddsPublisher->delete_datawriter(writer);
    }
    if (ddsPublisher) {
      participant->delete_publisher(ddsPublisher);
    }
    if (topic) {
      participant->delete_topic(topic);
    }
    DomainParticipantFactory::get_instance()->delete_participant(participant);
  }
}

void DdsPublisherModule::handleMessage(cMessage* msg) {
  if (msg == timer) {
    HelloWorld hw;
    hw.index(++seq);
    hw.message("Hello from OMNeT++");

    writer->write(&hw);

    EV << "[DDS PUB] Published index=" << hw.index() << "\n";

    scheduleAt(simTime() + 1.0, timer);
  }
}

void DdsPublisherModule::finish() {
  // nothing
}