// src/DdsTsnSubscriberApp.cpp
#include <omnetpp.h>

#include <fastdds/dds/core/ReturnCode.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>

#include "CustomTransport.hpp"
#include "CustomTransportDescriptor.hpp"
#include "HelloWorldPubSubTypes.hpp"
#include "inet/common/InitStages.h"

using namespace omnetpp;
using namespace inet;
using namespace eprosima::fastdds::dds;

class DdsTsnSubscriberApp : public cSimpleModule {
 private:
  cMessage* pollTimer_ = nullptr;
  simtime_t pollInterval_ = 0.01;

  // DDS entities
  eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
  eprosima::fastdds::dds::Subscriber* ddsSubscriber_ = nullptr;
  eprosima::fastdds::dds::Topic* topic_ = nullptr;
  eprosima::fastdds::dds::DataReader* reader_ = nullptr;
  HelloWorldPubSubType typeSupport_;
  bool participant_owned_ = false;

 protected:
  virtual int numInitStages() const override { return NUM_INIT_STAGES; }
  virtual void initialize(int stage) override;
  virtual void handleMessage(cMessage* msg) override;
  virtual void finish() override;
};

Define_Module(DdsTsnSubscriberApp);

void DdsTsnSubscriberApp::initialize(int stage) {
  cSimpleModule::initialize(stage);

  if (stage == INITSTAGE_LOCAL) {
    pollInterval_ = par("pollInterval");
    pollTimer_ = new cMessage("ddsPoll");
  } else if (stage == INITSTAGE_APPLICATION_LAYER) {
    // Try to reuse an existing participant (domain 0), otherwise create our own
    // with custom transport
    participant_ =
        eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
            ->lookup_participant(0);

    if (!participant_) {
      DomainParticipantQos pqos;
      pqos.transport().use_builtin_transports = false;
      auto desc = std::make_shared<CustomTransportDescriptor>();
      pqos.transport().user_transports.push_back(desc);

      participant_ =
          DomainParticipantFactory::get_instance()->create_participant(0, pqos);
      if (!participant_)
        throw cRuntimeError(
            "Failed to create DomainParticipant for DDS TSN Subscriber");
      participant_owned_ = true;
      EV_INFO << "[DDS TSN SUB] Participant ready: " << participant_ << "\n";

      // Register type and create subscriber/datareader as a normal DDS app
      // would
      typeSupport_.set_name("HelloWorld");
      participant_->register_type(TypeSupport(&typeSupport_));
      EV_INFO << "[DDS TSN SUB] Registered type HelloWorld\n";

      ddsSubscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
      if (!ddsSubscriber_) throw cRuntimeError("Failed to create Subscriber");
      EV_INFO << "[DDS TSN SUB] Subscriber created: " << ddsSubscriber_ << "\n";
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
          topic_ =
              participant_->find_topic("HelloWorldTopic", Duration_t{1, 0});
        }
      }
      if (!topic_)
        throw cRuntimeError("Failed to create or find Topic 'HelloWorldTopic'");

      if (created_topic)
        EV_INFO << "[DDS TSN SUB] Created Topic 'HelloWorldTopic'\n";
      else
        EV_INFO << "[DDS TSN SUB] Using existing Topic 'HelloWorldTopic'\n";

      EV_INFO << "[DDS TSN SUB] Creating DataReader for topic " << topic_
              << "\n";
      reader_ = ddsSubscriber_->create_datareader(
          topic_, DATAREADER_QOS_DEFAULT, nullptr);
      if (!reader_) throw cRuntimeError("Failed to create DataReader");
      EV_INFO << "[DDS TSN SUB] DataReader created: " << reader_ << "\n";

      // Start polling for data
      EV_INFO << "[DDS TSN SUB] Scheduling poll timer (interval="
              << pollInterval_ << ")\n";
      scheduleAt(simTime() + pollInterval_, pollTimer_);
    }
  }
}

void DdsTsnSubscriberApp::handleMessage(cMessage* msg) {
  if (msg == pollTimer_) {
    if (!reader_) {
      scheduleAt(simTime() + pollInterval_, pollTimer_);
      return;
    }

    // Poll for all available samples
    HelloWorld sample;
    eprosima::fastdds::dds::SampleInfo info;
    while (reader_->take_next_sample(&sample, &info) == RETCODE_OK) {
      if (info.valid_data) {
        simtime_t latency = SIMTIME_ZERO;
        EV_INFO << "[DDS TSN SUB] Seq=" << sample.index() << " msg=\""
                << sample.message() << "\" latency=" << latency << "\n";
      }
    }

    scheduleAt(simTime() + pollInterval_, pollTimer_);
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