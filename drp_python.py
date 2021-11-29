import sys

import numpy
import posix_ipc
import mmap

thread_num = sys.argv[1]

print(f"Starting python side (thread {thread_num})")

print(
    f"Connecting to /mq{thread_num}cs and /mq{thread_num}sc on the python side "
    f"(thread {thread_num})"
)

mq_cs = posix_ipc.MessageQueue(f"/mq{thread_num}cs", flags=0, read=False, write=True)
mq_sc = posix_ipc.MessageQueue(f"/mq{thread_num}sc", flags=0, read=True, write=False)
print(f"Connected on the python side (thread {thread_num})")

shm_s = posix_ipc.SharedMemory(f"/shm{thread_num}s")
shm_c = posix_ipc.SharedMemory(f"/shm{thread_num}c")
mem_s = mmap.mmap(shm_s.fd, 40000)
mem_c = mmap.mmap(shm_c.fd, 40000)

for array_index in range(100 * 100):

    message, priority = mq_sc.receive()
    print(
        f"Test {array_index}: Received request from C++ process (thread {thread_num})"
    )

    data_array_s = numpy.ndarray((100 * 100), dtype=numpy.float32, buffer=mem_s)
    data_array_c = numpy.ndarray((100 * 100), dtype=numpy.float32, buffer=mem_c)

    data_array_c[:] = data_array_s[:]
    if array_index % 2 == 1:
        data_array_c[array_index] += 1.0

    print(
        f"Test {array_index}: Current array sum: {data_array_s.sum()} (thread {thread_num})"
    )

    mq_cs.send("go")

print(f"Stopping python side (thread {thread_num})")
mq_cs.close()
mq_sc.close()
shm_s.close_fd()
shm_c.close_fd()
