#include <unistd.h>
#include <string>
#include <iostream>
#include <sstream>
#include <thread>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>


int mq_sc_id[2];
int mq_cs_id[2];
int shm_sc_id[2];
int shm_cs_id[2];



void exit_gracefully( int signum ) {
   std::cout << "Interrupt signal (" << signum << ") received. Cleaning up!\n";

   msgctl(mq_sc_id[0], IPC_RMID, NULL);
   msgctl(mq_cs_id[0], IPC_RMID, NULL);
   msgctl(mq_sc_id[1], IPC_RMID, NULL);
   msgctl(mq_cs_id[1], IPC_RMID, NULL);
   shmctl(shm_sc_id[0], IPC_RMID, NULL);
   shmctl(shm_cs_id[0], IPC_RMID, NULL);
   shmctl(shm_sc_id[1], IPC_RMID, NULL);
   shmctl(shm_cs_id[1], IPC_RMID, NULL);
  
   exit(signum);  
}


float sum_array(float *data_array) {
    float sum = 0.0;
    for (int i=0; i<100*100; i++) {
        if (data_array[i] != 0.0 && data_array[i] != 1.0) {
            std::cerr << "Warning: an element has a value that is not 0.0 or 1.0: "
                      << data_array[i] << std::endl;
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
    // Set up pipes for communication with sub processes

    int pipefd_stdout[2], pipefd_stderr[2];
    if (pipe(pipefd_stdout) == -1 || pipe(pipefd_stderr) == -1)
    {
        std::cerr << "Fatal error: error creating stdout pipe" << std::endl;
        return 1;
    }

    // Fork

    pid_t child_pid = fork();
    
    if (child_pid == pid_t(0))
    {
        // Set up pipes in child process

        // dup2(pipefd_stdout[1],1);
        // dup2(pipefd_stderr[1],2);
        // close(pipefd_stdout[0]);
        // close(pipefd_stdout[1]);
        // close(pipefd_stderr[0]);
        // close(pipefd_stderr[1]);

        // Executing external code

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

        // Set up pipes in parent process

        // close(pipefd_stdout[1]);
        // close(pipefd_stderr[1]);

        // Creating message queues

        std::cout << "Creating message queues (thread " << thread_num << ")"
                  << std::endl;
        mq_sc_id[thread_num] = msgget(200000 + (10000 * thread_num), IPC_CREAT | 0666);
        if ( mq_sc_id[thread_num] == -1 ) {
            std::cerr << "Error in creating server-client message queue (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            kill(child_pid, SIGTERM);                      
            return 1;
        }
        mq_cs_id[thread_num] = msgget(200001 + (10000 * thread_num), IPC_CREAT | 0666);
        if ( mq_cs_id[thread_num] == -1 ) {
            std::cerr << "Error in creating client-server message queue (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            kill(child_pid, SIGTERM);  
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            return 1;
        }

        std::cout << "Message queues created (thread " << thread_num << ")"
                  << std::endl;

        // // Creating shared memory

        std::cout << "Creating shared memory blocks (thread " << thread_num << ")" <<
                     std::endl;

        shm_sc_id[thread_num] = shmget(
            200002 + (10000 * thread_num),
            sizeof(float)*100*100,
            IPC_CREAT | IPC_EXCL | 0666
        );
        if ( shm_sc_id[thread_num] == -1 ) {
            std::cerr << "Error in creating server-client shared memory (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            kill(child_pid, SIGTERM);                      
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            return 1;
        }
        float *data_array_sc = (float*)shmat(shm_sc_id[thread_num], NULL, 0);
        if ( data_array_sc == (void*)-1 ) {
            std::cerr << "Error attaching server-client shared memory (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            kill(child_pid, SIGTERM);                      
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            return 1;
        }

        std::cout << "Server-client shared memory created  (thread " << thread_num
                  << ")" << std::endl;

        shm_cs_id[thread_num] = shmget(
            200003 + (10000 * thread_num),
            sizeof(float)*100*100,
            IPC_CREAT | IPC_EXCL| 0666
        );
        if ( shm_cs_id[thread_num] == -1 ) {
            std::cerr << "Error in creating client-server shared memory (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            kill(child_pid, SIGTERM);                      
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            return 1;
        }
        float *data_array_cs = (float*)shmat(shm_cs_id[thread_num], NULL, 0);
        if ( data_array_sc == (void*)-1 ) {
            std::cerr << "Error in attaching client-server shared memory (thread "
                      << thread_num << ")" << " - Errno: " << errno << std::endl;
            kill(child_pid, SIGTERM);                      
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            return 1;
        }

        std::cout << "Client-server shared memory created  (thread " << thread_num
                  << ")" << std::endl;

        // Initializing message arrays

        int go_msg[1];
        int recv_msg[1];
        go_msg[0] = 1;   
        for (int i = 0; i < 100*100; i++)
        {
            // Send message to python side

            std::cout << "Test " << i << ": Sending request to python "
                      << "(thread " << thread_num << ")" << std::endl;

            int ret_send = msgsnd(mq_sc_id[thread_num], &go_msg, sizeof(go_msg), 0);
            if ( ret_send == -1 ) {
                std::cerr << "Error sending message (thread " << thread_num << ")"
                          << " - Errno: " << errno << std::endl;
                kill(child_pid, SIGTERM);                      
                msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
                msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
                shmdt(data_array_sc);
                shmdt(data_array_cs);
                shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
                shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
                return 1;
            }

            // Receive message from python side

            int ret_receive = msgrcv(mq_cs_id[thread_num], &recv_msg, sizeof(recv_msg), 0, 0);
            if ( ret_receive == -1 ) {
                std::cerr << "Error receiving message (thread " << thread_num << ")"
                          << std::endl;   
                kill(child_pid, SIGTERM);                      
                msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
                msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
                shmdt(data_array_sc);
                shmdt(data_array_cs);
                shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
                shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
                return 1;
            }
            std::cout << "Test " << i << ": Received reply from  python "
                      << "(thread " << thread_num << ")" << std::endl;

            // Copy array from client to server shared memory

            memcpy(data_array_sc, data_array_cs, sizeof(float)*100*100);

            // Update array and print sum

            if ( i % 2 == 0) {
                data_array_sc[i] += 1.0;
            }

            std::cout << "Test " << i << ": Current array sum: "
                      << sum_array(data_array_sc)
                      << "(thread " << thread_num << ")" << std::endl;

            if (sum_array(data_array_cs) == 10000.0) {
                std::cerr << "Test " << i << ": Current array sum: "
                        << sum_array(data_array_sc)
                        << "(thread " << thread_num << ")" << std::endl;
            }

        }

        // Shut down thread

        std::cout << "Stopping C++ side "
                  << "(thread " << thread_num << ")" << std::endl;

        msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
        msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
        shmdt(data_array_sc);
        shmdt(data_array_cs);
        shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
        shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);

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

    signal(SIGINT, exit_gracefully);

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
