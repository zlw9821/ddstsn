#include "ChatPubSubTypes.hpp"

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>

#include <atomic>
#include <csignal>
#include <thread>
#include <iostream>

using namespace eprosima::fastdds::dds;

static std::atomic_bool running{true};

void signal_handler(int)
{
    running = false;
}

int main()
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 1. 手动加载 XML 配置文件
    // load_profiles 会返回 ReturnCode_t，成功为 RETCODE_OK
    if (DomainParticipantFactory::get_instance()->load_XML_profiles_file(
    "pub.xml"
) != RETCODE_OK)
    {
        std::cerr << "error: can not load pub.xml" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 1. 使用 XML 中的 profile 创建 Participant 
    // -------------------------------------------------
    // 这里使用 create_participant_with_profile，名称必须与 pub.xml 中的 profile_name 一致
    DomainParticipant* participant =
        DomainParticipantFactory::get_instance()->create_participant_with_profile(0, "pub_participant");

    if (!participant)
    {
        std::cerr << "Failed to create DomainParticipant using profile" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 2. 注册类型 
    // -------------------------------------------------
    TypeSupport type(new ChatMessagePubSubType());
    if (type.register_type(participant) != 0)
    {
        std::cerr << "Failed to register type" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 3. 创建 Topic 
    // -------------------------------------------------
    Topic* topic =
        participant->create_topic(
            "ChatTopic",
            type.get_type_name(),
            TOPIC_QOS_DEFAULT);

    if (!topic)
    {
        std::cerr << "Failed to create topic" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 4. 创建 Publisher 
    // -------------------------------------------------
    Publisher* publisher =
        participant->create_publisher(PUBLISHER_QOS_DEFAULT);

    if (!publisher)
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 5. 创建 DataWriter 
    // -------------------------------------------------
    DataWriterQos writer_qos;
    publisher->get_default_datawriter_qos(writer_qos);
    
    // 设置可靠传输和持久性（可选，建议与 Sub 端一致）
    writer_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    writer_qos.history().kind     = KEEP_LAST_HISTORY_QOS;
    writer_qos.history().depth    = 10;

    DataWriter* writer =
        publisher->create_datawriter(topic, writer_qos);

    if (!writer)
    {
        std::cerr << "Failed to create DataWriter" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 6. 发送循环 
    // -------------------------------------------------
    ChatMessage data;
    data.user_id(101);
    int count = 0;

    std::cout << "Publisher running. Sending messages via Unicast..." << std::endl;

    while (running)
    {
        data.message("Hello FastDDS " + std::to_string(count++));
        writer->write(&data);
        std::cout << "[Sent] " << data.message() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // -------------------------------------------------
    // 7. 清理 
    // -------------------------------------------------
    participant->delete_contained_entities();
    DomainParticipantFactory::get_instance()->delete_participant(participant);

    return 0;
}