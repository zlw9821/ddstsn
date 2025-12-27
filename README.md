# ddstsn

## Simulation guidance: handling simulation vs real time

When running Fast DDS inside OMNeT++/INET simulations, the simulator wall-clock vs simulation-time mismatch can cause DDS timers (heartbeats, liveliness, retransmissions) to behave incorrectly. Recommended approaches:

- Prefer enabling a real-time scheduler if your environment supports it (set `real-time-scheduler = true` in `omnetpp.ini`). This makes the simulator follow wall clock and keeps DDS timers aligned.

- If real-time scheduling is not possible or you expect the simulation to slow down, increase DDS reliability timeouts (heartbeat, lease durations) by a safe factor to avoid spurious retransmissions or liveliness loss. Example DataWriter QoS adjustments:

```cpp
DataWriterQos writer_qos = DATAWRITER_QOS_DEFAULT;
writer_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
// make heartbeats 10x more lenient
writer_qos.reliability().heartbeatPeriod.seconds = 1;

writer_ = ddsPublisher_->create_datawriter(topic_, writer_qos);
```

- Alternatively, consider using FlowController or application-level pacing to ensure DDS sends are synchronized with simulation events.

- We provide a `qosScaleFactor` parameter in the NED for both publisher and subscriber to indicate intended scaling; the code logs a recommendation when this is set but does not automatically change Fast DDS internals (which requires changing DDS QoS profiles or the DDS library itself).

- Discovery traffic can be starved if it shares a queue with bulk best-effort traffic and the TAS schedule never opens that queue. We added a config example to `omnetpp.ini` that creates a dedicated `discovery` stream (matching UDP ports 7400-7450) and a short periodic TAS window so PDP/EDP packets can progress. If you need more robustness, increase the discovery window or move discovery into a separate, rarely-used queue with guaranteed periodic openings.

If you'd like, I can implement automated QoS profile generation (XML) and have the Participant load it at startup so timers are adjusted automatically. Reply with "Generate QoS profiles" to proceed.
