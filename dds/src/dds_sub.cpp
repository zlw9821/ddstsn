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
#include <atomic>
#include <csignal>

using namespace eprosima::fastdds::dds;

static std::atomic_bool running{true};

void signal_handler(int) { running = false; }

// 数据监听器类
class MyListener : public DataReaderListener {
public:
    void on_data_available(DataReader* reader) override {
        ChatMessage data;
        SampleInfo info;

        while (reader->take_next_sample(&data, &info) == RETCODE_OK) {
            if (info.valid_data) {
                std::cout << "[Received] User " << data.user_id() 
                          << ": " << data.message() << std::endl;
            }
        }
    }

    void on_subscription_matched(DataReader* reader, const SubscriptionMatchedStatus& info) override {
        if (info.current_count_change == 1) {
            std::cout << ">> Match Found: Publisher connected via Unicast!" << std::endl;
        } else if (info.current_count_change == -1) {
            std::cout << ">> Match Lost: Publisher disconnected." << std::endl;
        }
    }
};

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (DomainParticipantFactory::get_instance()->load_XML_profiles_file(
    "sub.xml"
) != RETCODE_OK)
    {
        std::cerr << "error: can not load sub.xml" << std::endl;
        return 1;
    }

    // 1. 使用 XML 中的 profile 创建 Participant
    // 名称需对应 sub.xml 中的 sub_participant
    DomainParticipant* participant = 
        DomainParticipantFactory::get_instance()->create_participant_with_profile(0, "sub_participant");

    if (!participant) {
        std::cerr << "Failed to create DomainParticipant using profile" << std::endl;
        return 1;
    }

    // 2. 注册类型
    TypeSupport type(new ChatMessagePubSubType());
    type.register_type(participant);

    // 3. 创建 Topic
    Topic* topic = participant->create_topic("ChatTopic", type.get_type_name(), TOPIC_QOS_DEFAULT);

    // 4. 创建 Subscriber
    Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);

    // 5. 创建 DataReader 并配置 QoS 与发布端匹配
    DataReaderQos reader_qos;
    subscriber->get_default_datareader_qos(reader_qos);
    reader_qos.reliability().kind = RELIABLE_RELIABILITY_QOS; // 必须匹配发布端
    
    MyListener listener;
    DataReader* reader = subscriber->create_datareader(topic, reader_qos, &listener);

    if (!reader) {
        std::cerr << "Failed to create DataReader" << std::endl;
        return 1;
    }

    std::cout << "Subscriber running. Waiting for Unicast messages..." << std::endl;

    while(running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 6. 清理
    participant->delete_contained_entities();
    DomainParticipantFactory::get_instance()->delete_participant(participant);

    return 0;
}