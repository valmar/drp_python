import sys
import time

import zmq

print(sys.argv)
thread_num = sys.argv[1]
context = zmq.Context()

print(f"Starting python side (thread {thread_num})")

print(f"Connecting to ipc:///tmp/drpsocket{thread_num} on the python side "
      "(thread {thread_num})")
socket = context.socket(zmq.PAIR)
socket.connect(f"ipc:///tmp/drpsocket{thread_num}")
print(f"Connected on the python side (thread {thread_num})")

for test in range(10):

    message = socket.recv()
    print(f"Test {test}: Received request from C++ process (thread {thread_num})")

    time.sleep(1)

    print(f"Test {test}: Sending reply to C++ process (thread {thread_num})")
    socket.send(b"Hello")
