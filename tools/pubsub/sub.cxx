// ChatSubscriber.cpp
#include "ChatPubSubTypes.hpp"

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/core/ReturnCode.hpp>

#include <thread>
#include <chrono>
#include <iostream>

using namespace eprosima::fastdds::dds;

// 监听器类：回调函数在这里触发
class MyListener : public DataReaderListener {
public:
    void on_data_available(DataReader* reader) override {
        ChatMessage data;
        SampleInfo info;

        // 取出数据
        while (reader->take_next_sample(&data, &info) == eprosima::fastdds::dds::RETCODE_OK) {
            if (info.valid_data) {
                std::cout << "[Received] User " << data.user_id() 
                          << ": " << data.message() << std::endl;
            }
        }
    }

    void on_subscription_matched(DataReader* reader, const SubscriptionMatchedStatus& info) override {
        if (info.current_count_change == 1) {
            std::cout << ">> Match Found: Publisher connected!" << std::endl;
        } else if (info.current_count_change == -1) {
            std::cout << ">> Match Lost: Publisher disconnected." << std::endl;
        }
    }
};

int main() {
    // 1. 创建 Participant
    DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);

    // 2. 注册类型
    TypeSupport type(new ChatMessagePubSubType());
    type.register_type(participant);

    // 3. 创建 Topic
    Topic* topic = participant->create_topic("ChatTopic", type.get_type_name(), TOPIC_QOS_DEFAULT);

    // 4. 创建 Subscriber
    Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);

    // 5. 创建 DataReader 并绑定 Listener
    MyListener listener;
    // 注意：这里需要传入 Listener 指针，并掩码设置我们要监听所有状态
    DataReader* reader = subscriber->create_datareader(topic, DATAREADER_QOS_DEFAULT, &listener);

    std::cout << "Waiting for messages... (Press Ctrl+C to stop)" << std::endl;

    // 阻塞主线程，否则 main 结束程序就退出了
    while(true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}

