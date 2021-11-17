import zmq
import sys

context = zmq.Context()
thread_num = int(sys.argv[1])

#  Socket to talk to server
print(f"Connecting to server{thread_num} at /tmp/drpsocket{thread_num}")
socket = context.socket(zmq.PAIR)
socket.connect(f"ipc:///tmp/drpsocket{thread_num}")

#  Do 10 requests, waiting each time for a response
for request in range(10):
    print(f"Sending request server{thread_num}")
    socket.send(b"Hello")

    #  Get the reply.
    message = socket.recv()
    print(f"Received reply from server{thread_num}")
