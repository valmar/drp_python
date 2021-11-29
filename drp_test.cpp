#include <unistd.h>
#include <string>
#include <iostream>
#include <sstream>
#include <thread>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <mqueue.h>
#include <sys/wait.h>
#include <sys/mman.h>


float sum_array(float *data_array) {
    float sum = 0.0;
    for (int i=0; i<100*100; i++) {
        if (data_array[i] != 0.0 && data_array[i] != 1.0) {
            std::cerr << "Warning: an element has a value that is not 0.0 or 1.0"
                      << std::endl;
        }
        sum += data_array[i];
    }
    return sum;
}


void read_pipe_to_the_end(int pipe, int thread_num)
{

    char read_buffer[1024];
    int nbytes;

    int flags = fcntl(pipe, F_GETFL);
    flags |= O_NONBLOCK;
    int ret_val = fcntl(pipe, F_SETFL, flags);

    while (1)
    {
        nbytes = read(pipe, read_buffer, sizeof(read_buffer));
        if (nbytes > 0 && nbytes < 1024)
        {
            read_buffer[nbytes] = 0;
            std::cout << "Output from child " << thread_num << ": " << read_buffer;
        }
        else if (nbytes == 0)
        {
            return;
        }
        else if (nbytes < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
        {
            return;
        }
        else
        {
            std::cout << "Error reading from pipe: " << errno << std::endl;
            return;
        }
    }
}

int start_server_client(int thread_num, char *argv[])
{
    int pipefd_stdout[2], pipefd_stderr[2];

    if (pipe(pipefd_stdout) == -1 || pipe(pipefd_stderr) == -1)
    {
        std::cerr << "Fatal error: error creating stdout pipe" << std::endl;
        return 1;
    }

    pid_t child_pid = fork();

    if (child_pid == pid_t(0))
    {
        // dup2(pipefd_stdout[1],1);
        // dup2(pipefd_stderr[1],2);
        // close(pipefd_stdout[0]);
        // close(pipefd_stdout[1]);
        // close(pipefd_stderr[0]);
        // close(pipefd_stderr[1]);

        int ret_exec = execlp(
            "python",
            "python",
            "-u",
            argv[1],
            std::to_string(thread_num).c_str(),
            NULL);
        return 0;
    }
    else
    {
        std::cout << "Starting C++ side (thread " << thread_num << ")" << std::endl;
        
        // close(pipefd_stdout[1]);
        // close(pipefd_stderr[1]);

        std::stringstream mq_key_str_cs;
        std::stringstream mq_key_str_sc;
        mq_key_str_cs <<
            "/mq" <<
            thread_num <<
            "cs";
        mq_key_str_sc <<
            "/mq" <<
            thread_num <<
            "sc";
        std::string mq_key_cs = mq_key_str_cs.str();
        std::string mq_key_sc = mq_key_str_sc.str();
        std::cout << "Creating message queues (thread " << thread_num << ")"
                  << std::endl;

        mq_attr attrs;
        attrs.mq_flags = 0;
        attrs.mq_maxmsg = 1;
        attrs.mq_msgsize = 3;
        attrs.mq_curmsgs = 0;
        
        mqd_t mq_sc = mq_open(
            mq_key_sc.c_str(),
            (O_CREAT | O_WRONLY),
            0664,
            &attrs
        );
        if ( mq_sc == -1 ) {
            std::cerr << "Error in creating server-client message queue (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            return 1;
        }
        
        mqd_t mq_cs = mq_open(
            mq_key_cs.c_str(),
            (O_CREAT | O_RDONLY),
            0664,
            &attrs
        );
        if ( mq_cs == -1 ) {
            std::cerr << "Error in creating client-server message queue (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            return 1;
        }
        std::cout << "Message queue created (thread " << thread_num << ")"
                  << std::endl;

        std::stringstream shm_key_str_s;
        std::stringstream shm_key_str_c;
        shm_key_str_s << "/shm" << thread_num << "s";
        shm_key_str_c << "/shm" << thread_num << "c";
        std::string shm_key_s = shm_key_str_s.str();
        std::string shm_key_c = shm_key_str_c.str();
        std::cout << "Creating shared memory blocks (thread " << thread_num << ")" <<
                     std::endl;

        int shm_fd_s = shm_open(shm_key_s.c_str(), O_CREAT | O_RDWR, 0666);
        if ( shm_fd_s == -1 ) {
            std::cerr << "Error in creating server's shared memory (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            return 1;
        }
        ftruncate(shm_fd_s, sizeof(float)*100*100);
        void *shm_s_ptr = mmap(NULL, sizeof(float)*100*100, PROT_WRITE, MAP_SHARED,
                          shm_fd_s, 0);
        std::cout << "Server's shared memory created  (thread " << thread_num
                  << ")" << std::endl;
   
        int shm_fd_c = shm_open(shm_key_c.c_str(), O_CREAT | O_RDWR, 0666);
        if ( shm_fd_c == -1 ) {
            std::cerr << "Error in creating client's shared memory (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            return 1;
        }
        ftruncate(shm_fd_c, sizeof(float)*100*100);
        void *shm_c_ptr = mmap(NULL, sizeof(float)*100*100, PROT_READ, MAP_SHARED,
                          shm_fd_c, 0);
        std::cout << "Client's shared memory created  (thread " << thread_num
                  << ")" << std::endl;

        float *data_array_s = (float*)shm_s_ptr;
        float *data_array_c = (float*)shm_c_ptr;

        for (int i = 0; i<100*100; i++) {
            data_array_s[i] = 0;
        }

        char message[3];    
        for (int i = 0; i < 100*100; i++)
        {

            std::cout << "Test " << i << ": Sending request to python "
                      << "(thread " << thread_num << ")" << std::endl;

            int res_send = mq_send(mq_sc, "go", 3, 1);
            if ( res_send == -1 ) {
                std::cerr << "Error sending message (thread " << thread_num << ")"
                          << " - Errno: " << errno << std::endl;
                return 1;
            }

            std::cout << "Test " << i << ": Waiting for answer from python "
                      << "(thread " << thread_num << ")" << std::endl;

            int ret_receive = mq_receive(mq_cs, message, 3, NULL);
            if ( ret_receive == -1 ) {
                std::cerr << "Error receiving message (thread " << thread_num << ")"
                          << std::endl;   
                return 1;
            }
            std::cout << "Test " << i << ": Received reply from  python "
                      << "(thread " << thread_num << ")" << std::endl;

            memcpy(data_array_s, data_array_c, sizeof(float)*100*100);
            if ( i % 2 == 0) {
                data_array_s[i] += 1.0;
            }

            std::cout << "Test " << i << ": Current array sum: "
                      << sum_array(data_array_s)
                      << "(thread " << thread_num << ")" << std::endl;
        }

        mq_close(mq_cs);
        mq_close(mq_sc);
        mq_unlink(mq_key_str_cs.str().c_str());
        mq_unlink(mq_key_str_sc.str().c_str());
        shm_unlink(shm_key_str_c.str().c_str());
        shm_unlink(shm_key_str_s.str().c_str());

        int status;
        pid_t wait_pid = waitpid(child_pid, &status, 0);

        // read_pipe_to_the_end(pipefd_stdout[0], thread_num);
        // read_pipe_to_the_end(pipefd_stderr[0], thread_num);

        // close(pipefd_stdout[0]);
        // close(pipefd_stderr[0]);

        return wait_pid == child_pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Usage: drp_test <python file>\n");
        exit(1);
    }
    std::thread first(start_server_client, 0, argv);
    std::thread second(start_server_client, 1, argv);
    first.join();
    second.join();
    return 0;
}
