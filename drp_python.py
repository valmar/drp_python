import sys

import numpy
import sysv_ipc
from psana import dgram
from struct import unpack

transition_id = {
        0: "ClearReadout",
        1: "Reset",
        2: "Configure",
        3: "Unconfigure",
        4: "BeginRun",
        5: "EndRun",
        6: "BeginStep",
        7: "EndStep",
        8: "Enable",
        9: "Disable",
        10: "SlowUpdate",
        11: "Unused_11",
        12: "L1Accept",
        13: "NumberOf",
}

def datagram_size(view):
    iExt = 5                    # Index of extent field, in units of uint32_t
    txSize = 3 * 4              # sizeof(XtcData::TransitionBase)
    return txSize + numpy.array(view, copy=False).view(dtype=numpy.uint32)[iExt]

thread_num = int(sys.argv[1])

print(f"[Python] Starting python side (thread {thread_num})")

try:
    mq_sc = sysv_ipc.MessageQueue(200000 + (10000 * thread_num))
except sysv_ipc.Error as exp:
    print(
        f"[Python] Error connecting to server-client message on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    sys.exit(1)

try:
    mq_cs = sysv_ipc.MessageQueue(200001 + (10000 * thread_num))
except sysv_ipc.Error as exp:
    print(
        f"[Python] Error connecting to client-server message queue on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    sys.exit(1)

print(f"[Python] Connected to message queues on the python side (thread {thread_num})")

try:
    shm_sc = sysv_ipc.SharedMemory(200002 + (10000 * thread_num), size=128000)
except sysv_ipc.Error as exp:
    print(
        f"[Python] Error connecting to server-client shared memory  on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    sys.exit(1)

try:
    shm_cs = sysv_ipc.SharedMemory(200003 + (10000 * thread_num), size=128000)
except sysv_ipc.Error as exp:
    print(
        f"[Python] Error connecting to client-server shared memory  on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    shm_sc.detach()
    sys.exit(1)

print(f"[Python] Setup shared memory on the python side (thread {thread_num})")

message, priority = mq_sc.receive()

num_event = 0

config_dgram = None

while message != b"s":
    print(
        f"[Python] Received message from C++ process (thread {thread_num})"
    )

    num_event += 1

    view = memoryview(shm_sc) 

    if message == b"c":
        print("[Python] Received a configure transition, storing it")
        datagram = dgram.Dgram(view=memoryview(shm_sc))
    else:
        datagram = dgram.Dgram(view=memoryview(shm_sc), config=config_dgram)

    datagram_timestamp = datagram.timestamp()
    timestamp_low  =  datagram_timestamp        & 0xffffffff
    timestamp_high = (datagram_timestamp >> 32) & 0xffffffff
    datagram_transition_id = transition_id[datagram.service()]

    print(f"[Python] Datagram information from python side - "
          f"Timestamp: {timestamp_high}.{timestamp_low}, "
          f"Service: {datagram_transition_id}, "
          f"Size: {datagram._size}")

    if datagram_transition_id == "Configure":
        byte_array_copy = bytes(view[:datagram_size(view)])
        config_dgram = dgram.Dgram(view=byte_array_copy)

    mq_cs.send(b"g")
    print(
        f"[Python] Sent message to C++ process (thread {thread_num})"
    )
    message, priority = mq_sc.receive()

print(f"[Python] Stopping python side (thread {thread_num})")
shm_sc.detach()
shm_cs.detach()
