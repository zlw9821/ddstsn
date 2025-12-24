#include <omnetpp.h>

#include <fastdds/dds/core/ReturnCode.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>

#include "HelloWorldPubSubTypes.hpp"

using namespace omnetpp;
using namespace eprosima::fastdds::dds;

class DdsSubscriberModule : public cSimpleModule {
 private:
  cMessage* timer = nullptr;

  DomainParticipant* participant = nullptr;
  eprosima::fastdds::dds::Subscriber* dds_subscriber = nullptr;
  Topic* topic = nullptr;
  DataReader* reader = nullptr;

  HelloWorldPubSubType type;

 protected:
  virtual void initialize() override;
  virtual void handleMessage(cMessage* msg) override;
  virtual void finish() override;
  virtual ~DdsSubscriberModule();
};

Define_Module(DdsSubscriberModule);

//------------------------------------------------------------

void DdsSubscriberModule::initialize() {
  // 1s后开始第一次轮询，避开发现阶段
  timer = new cMessage("subtimer");
  scheduleAt(simTime() + 1.0, timer);

  DomainParticipantQos pqos;
  pqos.name("DDS_Sub_Participant");
  participant =
      DomainParticipantFactory::get_instance()->create_participant(0, pqos);
  if (!participant) throw cRuntimeError("Failed to create DomainParticipant");

  // 注册类型：封装为 TypeSupport 对象
  type.set_name("HelloWorld");
  participant->register_type(TypeSupport(&type));

  // 创建 Subscriber
  dds_subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
  if (!dds_subscriber) throw cRuntimeError("Failed to create DDS Subscriber");

  // 创建 Topic
  topic = participant->create_topic("HelloWorldTopic", "HelloWorld",
                                    TOPIC_QOS_DEFAULT);
  if (!topic) throw cRuntimeError("Failed to create DDS Topic");

  // 创建 DataReader (不使用 Listener，改用 OMNeT++ 轮询模式)
  reader =
      dds_subscriber->create_datareader(topic, DATAREADER_QOS_DEFAULT, nullptr);
  if (!reader) throw cRuntimeError("Failed to create DDS DataReader");
}

void DdsSubscriberModule::handleMessage(cMessage* msg) {
  if (msg == timer) {
    HelloWorld hw;
    SampleInfo info;

    // 尝试读取数据
    if (reader->take_next_sample(&hw, &info) == RETCODE_OK) {
      if (info.valid_data) {
        // 注意：生成的 IDL 成员 index 可能是 index() 方法
        EV << "[DDS SUB] Received index=" << hw.index()
           << " message=" << hw.message() << " at t=" << simTime() << "\n";
      }
    } else {
      // 没有任何新数据时的逻辑
      // EV << "[DDS SUB] No new sample available\n";
    }

    // 继续轮询
    scheduleAt(simTime() + 0.1, timer);  // 这里可以缩短轮询间隔，比如100ms
  } else {
    delete msg;
  }
}

void DdsSubscriberModule::finish() {
  // OMNeT++ 仿真结束时的清理
}

DdsSubscriberModule::~DdsSubscriberModule() {
  cancelAndDelete(timer);

  if (participant) {
    // 按照层级逐个销毁：Reader 归属于 Subscriber
    if (dds_subscriber) {
      if (reader) dds_subscriber->delete_datareader(reader);
      participant->delete_subscriber(dds_subscriber);
    }

    if (topic) {
      participant->delete_topic(topic);
    }

    DomainParticipantFactory::get_instance()->delete_participant(participant);
  }
}