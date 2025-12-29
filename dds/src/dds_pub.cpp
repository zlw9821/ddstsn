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

    // -------------------------------------------------
    // 1. Participant
    // -------------------------------------------------
    DomainParticipantQos participant_qos;
    participant_qos.name("ChatPublisherParticipant");

    DomainParticipant* participant =
        DomainParticipantFactory::get_instance()->create_participant(0, participant_qos);

    if (!participant)
    {
        std::cerr << "Failed to create DomainParticipant" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 2. Register type
    // -------------------------------------------------
    TypeSupport type(new ChatMessagePubSubType());

    if (type.register_type(participant) != 0)
    {
        std::cerr << "Failed to register type" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 3. Topic
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
    // 4. Publisher
    // -------------------------------------------------
    Publisher* publisher =
        participant->create_publisher(PUBLISHER_QOS_DEFAULT);

    if (!publisher)
    {
        std::cerr << "Failed to create publisher" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 5. DataWriter QoS (TSN-friendly)
    // -------------------------------------------------
    DataWriterQos writer_qos;
    publisher->get_default_datawriter_qos(writer_qos);

    writer_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    writer_qos.history().kind     = KEEP_LAST_HISTORY_QOS;
    writer_qos.history().depth    = 1;

    writer_qos.publish_mode().kind = ASYNCHRONOUS_PUBLISH_MODE;

    DataWriter* writer =
        publisher->create_datawriter(topic, writer_qos);

    if (!writer)
    {
        std::cerr << "Failed to create DataWriter" << std::endl;
        return 1;
    }

    // -------------------------------------------------
    // 6. Publish loop
    // -------------------------------------------------
    ChatMessage data;
    data.user_id(101);

    int count = 0;

    while (running)
    {
        data.message("Hello FastDDS " + std::to_string(count++));

        writer->write(&data);
        std::cout << "[Sent] " << data.message() << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // -------------------------------------------------
    // 7. Cleanup
    // -------------------------------------------------
    participant->delete_contained_entities();
    DomainParticipantFactory::get_instance()->delete_participant(participant);

    return 0;
}
