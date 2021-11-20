import sys

import zmq

print(sys.argv)
thread_num = sys.argv[1]
context = zmq.Context()

#  Socket to talk to server
print(f"Connecting to server{thread_num} at /tmp/drpsocket{thread_num}")
socket = context.socket(zmq.PAIR)
socket.connect(f"ipc:///tmp/drpsocket{thread_num}")

#  Do 10 requests, waiting each time for a response
for test in range(10):
    print(f"Test {test}: Sending request to server {thread_num}")
    socket.send(b"Hello")

    #  Get the reply.
    message = socket.recv()
    print(f"Test {test}: Received World from server {thread_num}")
