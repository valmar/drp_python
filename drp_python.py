import sys

import numpy
import sysv_ipc


thread_num = int(sys.argv[1])

print(f"Starting python side (thread {thread_num})")

try:
    mq_sc = sysv_ipc.MessageQueue(200000 + (10000 * thread_num))
except sysv_ipc.Error as exp:
    print(
        f"Error connecting to server-client message on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    sys.exit(1)

try:
    mq_cs = sysv_ipc.MessageQueue(200001 + (10000 * thread_num))
except sysv_ipc.Error as exp:
    print(
        f"Error connecting to client-server message queue on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    sys.exit(1)

print(f"Connected to message queues on the python side (thread {thread_num})")

try:
    shm_sc = sysv_ipc.SharedMemory(200002 + (10000 * thread_num), size=40000)
except sysv_ipc.Error as exp:
    print(
        f"Error connecting to server-client shared memory  on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    sys.exit(1)

try:
    shm_cs = sysv_ipc.SharedMemory(200003 + (10000 * thread_num), size=40000)
except sysv_ipc.Error as exp:
    print(
        f"Error connecting to client-server shared memory  on the python "
        f"side (thread {thread_num}) - Error: {exp}"
    )
    shm_sc.detach()
    sys.exit(1)


print(f"Setup shared memory on the python side (thread {thread_num})")

for array_index in range(100 * 100):

    message, priority = mq_sc.receive()
    print(
        f"Test {array_index}: Received request from C++ process (thread {thread_num})"
    )

    data_array_s = numpy.ndarray(
        (100 * 100),
        dtype=numpy.float32,
        buffer=memoryview(shm_sc)
    )
    data_array_c = numpy.ndarray(
        (100 * 100),
        dtype=numpy.float32,
        buffer=memoryview(shm_cs)
    )
    data_array_c[:] = data_array_s[:]
    if array_index % 2 == 1:
        data_array_c[array_index] += 1.0

    mq_cs.send("go")
    print(
        f"Test {array_index}: Sent answer to C++ process (thread {thread_num})"
    )

print(f"Stopping python side (thread {thread_num})")
shm_sc.detach()
shm_cs.detach()
