// ChatPublisher.cpp
#include "ChatPubSubTypes.hpp"

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>

#include <thread>
#include <iostream>

using namespace eprosima::fastdds::dds;

int main() {
    // 1. 创建 Participant (参与者)
    DomainParticipantQos participant_qos = PARTICIPANT_QOS_DEFAULT;
    DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, participant_qos);

    if (!participant) return 1;

    // 2. 注册数据类型
    TypeSupport type(new ChatMessagePubSubType());
    type.register_type(participant);

    // 3. 创建 Topic
    Topic* topic = participant->create_topic("ChatTopic", type.get_type_name(), TOPIC_QOS_DEFAULT);

    // 4. 创建 Publisher
    Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);

    // 5. 创建 DataWriter
    DataWriter* writer = publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT);

    // 6. 循环发送数据
    ChatMessage data;
    data.user_id(101); // 假设我是用户 101

    int count = 0;
    while (true) {
        data.message("Hello FastDDS " + std::to_string(count++));

        writer->write(&data);
        std::cout << "[Sent] " << data.message() << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 清理资源 (实际代码中通常在析构或信号处理中做)
    // participant->delete_contained_entities();
    // DomainParticipantFactory::get_instance()->delete_participant(participant);

    return 0;
}

