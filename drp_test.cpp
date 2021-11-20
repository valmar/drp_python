#include <unistd.h>
#include <string>
#include <iostream>
#include <thread>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include "zmq.hpp"


void read_pipe_to_end(int pipe, int thread_num) {

    char read_buffer[1024];
    int nbytes;

    int flags = fcntl(pipe, F_GETFL);
    flags |= O_NONBLOCK;
    int ret_val = fcntl(pipe, F_SETFL, flags);

    while (1) {
        nbytes = read(pipe, read_buffer, sizeof(read_buffer));
        if (nbytes > 0 && nbytes < 1024) {
            read_buffer[nbytes] = 0;
            std::cout << "Output from child " << thread_num << ": " << read_buffer;
        } else if (nbytes == 0) {
            return;
        } else if (nbytes < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            return;
        } else {
            std::cout << "Error reading from pipe: " << errno << std::endl;
            return;
        }
    }
}
    
int start_server_client(zmq::context_t *zmq_context, int thread_num, char *argv[])
{
    int pipefd_stdout[2],pipefd_stderr[2];

    if (pipe(pipefd_stdout) == -1 || pipe(pipefd_stderr) == -1) {
        std::cerr << "Fatal error: error creating stdout pipe" << std::endl;
        return 1;
    }

    pid_t child_pid = fork();

    if (child_pid == pid_t(0))
    {
        dup2(pipefd_stdout[1],1); 
        dup2(pipefd_stderr[1],2); 
        close(pipefd_stdout[0]);
        close(pipefd_stdout[1]);
        close(pipefd_stderr[0]);
        close(pipefd_stderr[1]);

        int ret_exec = execlp(
            "python",
            "python",
            "-u",
            argv[1],
            std::to_string(thread_num).c_str(),
            NULL
        );
        return 0;
    }
    else
    {        
        close(pipefd_stdout[1]);
        close(pipefd_stderr[1]);

        std::cout << "Starting C++ server " << thread_num << std::endl;
        zmq::socket_t socket(*zmq_context, zmq::socket_type::pair);

        std::stringstream socket_name;
        std::cout << "Connecting to ipc:///tmp/drpsocket" << thread_num << std::endl;
        socket_name << "ipc:///tmp/drpsocket" << thread_num;
        socket.bind(socket_name.str().c_str());
        std::cout << "Connected" << std::endl;

        read_pipe_to_end(pipefd_stdout[0], thread_num);
        read_pipe_to_end(pipefd_stderr[0], thread_num);

        std::cout << "Waiting for messages...." << std::endl;
        for (int i = 0; i < 10; i++)
        {
            zmq::message_t request;

            socket.recv(request, zmq::recv_flags::none);
            std::cout << "Test " << i << ": Received Hello from client "
                      << thread_num << std::endl;

            sleep(1);

            zmq::message_t reply(5);
            std::cout << "Test " << i << ": Replying to client "
                      << thread_num << std::endl;
            memcpy(reply.data(), "World", 5);
            socket.send(reply, zmq::send_flags::none);
            
            read_pipe_to_end(pipefd_stdout[0], thread_num);
            read_pipe_to_end(pipefd_stderr[0], thread_num);
        }


        int status;
        pid_t wait_pid = waitpid(child_pid, &status, 0);

        read_pipe_to_end(pipefd_stdout[0], thread_num);
        read_pipe_to_end(pipefd_stderr[0], thread_num);

        close(pipefd_stdout[0]);
        close(pipefd_stderr[0]);

        return wait_pid == child_pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
}

int main(int argc, char *argv[])
{
    zmq::context_t zmq_context(2);

    if (argc != 2)
    {
        printf("Usage: drp_test <python file>\n");
        exit(1);
    }
    std::thread first(start_server_client, &zmq_context, 0, argv);
    std::thread second(start_server_client, &zmq_context, 1, argv);
    first.join();
    second.join();
    return 0;
}
