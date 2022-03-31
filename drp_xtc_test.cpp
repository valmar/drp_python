#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include "xtcdata/xtc/XtcFileIterator.hh"
#include "xtcdata/xtc/XtcIterator.hh"
#include "xtcdata/xtc/TransitionId.hh"

struct ThreadArgs
{
    int thread_num;
    const char *python_script;
    const char *xtc_filename;
};

int mq_sc_id[2];
int mq_cs_id[2];
int shm_sc_id[2];
int shm_cs_id[2];
void *shm_sc_buf[2];
void *shm_cs_buf[2];

void exit_gracefully(int signum)
{
    std::cout << "[C++] Interrupt signal (" << signum << ") received. Cleaning up!\n";

    msgctl(mq_sc_id[0], IPC_RMID, NULL);
    msgctl(mq_cs_id[0], IPC_RMID, NULL);
    msgctl(mq_sc_id[1], IPC_RMID, NULL);
    msgctl(mq_cs_id[1], IPC_RMID, NULL);
    shmctl(shm_sc_id[0], IPC_RMID, NULL);
    shmctl(shm_cs_id[0], IPC_RMID, NULL);
    shmctl(shm_sc_id[1], IPC_RMID, NULL);
    shmctl(shm_cs_id[1], IPC_RMID, NULL);
    free(shm_sc_buf[0]);
    free(shm_cs_buf[0]);
    free(shm_sc_buf[1]);
    free(shm_cs_buf[1]);

    exit(signum);
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
        if (nbytes > 0)
        {
            read_buffer[nbytes] = 0;
            std::cout << read_buffer;
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
            std::cout << "[C++] Error reading from pipe: " << read_buffer << " "
                      << errno << std::endl;
            std::cout << "[C++] Exit error " << nbytes << " " << errno << std::endl;
            return;
        }
    }
}

void *start_server_client(void *input)
{

    ThreadArgs *thread_args = (ThreadArgs *)input;

    const char *python_script = thread_args->python_script;
    const char *xtc_filename = thread_args->xtc_filename;
    int thread_num = thread_args->thread_num;

    // Set up pipes for communication with sub processes

    int pipefd_stdout[2], pipefd_stderr[2];
    if (pipe(pipefd_stdout) == -1 || pipe(pipefd_stderr) == -1)
    {
        std::cerr << "[C++] Fatal error: error creating stdout and stderr pipes"
                  << std::endl;
        return NULL;
    }

    // Fork

    pid_t child_pid = fork();

    if (child_pid == pid_t(0))
    {
        // Set up pipes in child process
        dup2(pipefd_stdout[1], 1);
        dup2(pipefd_stderr[1], 2);
        close(pipefd_stdout[0]);
        close(pipefd_stdout[1]);
        close(pipefd_stderr[0]);
        close(pipefd_stderr[1]);

        // Executing external code
        int ret_exec = execlp(
            "python",
            "python",
            "-u",
            python_script,
            std::to_string(thread_num).c_str(),
            NULL);
        return NULL;
    }
    else
    {
        std::cout << "[C++] Starting C++ side (thread " << thread_num << ")"
                  << std::endl;

        // Set up pipes in parent process

        close(pipefd_stdout[1]);
        close(pipefd_stderr[1]);

        // Creating message queues

        std::cout << "[C++] Creating message queues (thread " << thread_num << ")"
                  << std::endl;
        mq_sc_id[thread_num] = msgget(200000 + (10000 * thread_num), IPC_CREAT | 0666);
        if (mq_sc_id[thread_num] == -1)
        {
            std::cerr << "[C++] Error in creating server-client message queue (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno << std::endl;
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }
        mq_cs_id[thread_num] = msgget(200001 + (10000 * thread_num), IPC_CREAT | 0666);
        if (mq_cs_id[thread_num] == -1)
        {
            std::cerr << "[C++] Error in creating client-server message queue (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }

        std::cout << "[C++] Message queues created (thread " << thread_num << ")"
                  << std::endl;

        // Creating shared memory

        std::cout << "[C++] Creating shared memory blocks (thread " << thread_num
                  << ")" << std::endl;

        unsigned pageSize = (unsigned)sysconf(_SC_PAGESIZE);
        size_t sizeofShm = 128000;
        unsigned remainder = sizeofShm % pageSize;
        if (remainder != 0)
        {
            sizeofShm += pageSize - remainder;
        }
        void *buffer_sc = NULL;
        int ret = posix_memalign(&buffer_sc, pageSize, sizeofShm);
        if (ret)
        {
            std::cerr << "[C++] Error in creating server-client memory buffer (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno
                      << " (" << std::string(strerror(errno)) << ")" << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }
        shm_sc_buf[thread_num] = buffer_sc;
        shm_sc_id[thread_num] = shmget(
            200002 + (10000 * thread_num),
            128000,
            IPC_CREAT | IPC_EXCL | 0666);
        if (shm_sc_id[thread_num] == -1)
        {
            std::cerr << "[C++] Error in creating server-client shared memory (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }
        float *data_array_sc = (float *)shmat(shm_sc_id[thread_num],
                                              buffer_sc, SHM_REMAP);
        if (data_array_sc == (void *)-1)
        {
            std::cerr << "[C++] Error attaching server-client shared memory (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            return NULL;
        }

        std::cout << "[C++] Server-client shared memory created (thread " << thread_num
                  << ")" << std::endl;

        void *buffer_cs = NULL;
        ret = posix_memalign(&buffer_cs, pageSize, sizeofShm);
        if (ret)
        {
            std::cerr << "[C++] Error in creating client-server memory buffer (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno
                      << " (" << std::string(strerror(errno)) << ")" << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            free(shm_sc_buf[thread_num]);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }
        shm_cs_buf[thread_num] = buffer_cs;
        shm_cs_id[thread_num] = shmget(
            200003 + (10000 * thread_num),
            128000,
            IPC_CREAT | IPC_EXCL | 0666);
        if (shm_cs_id[thread_num] == -1)
        {
            std::cerr << "[C++] Error in creating client-server shared memory (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            free(shm_sc_buf[thread_num]);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }
        float *data_array_cs = (float *)shmat(shm_cs_id[thread_num],
                                              buffer_cs, SHM_REMAP);
        if (data_array_cs == (void *)-1)
        {
            std::cerr << "Error in attaching client-server shared memory (thread "
                      << thread_num << ")"
                      << " - Errno: " << errno << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            free(shm_sc_buf[thread_num]);
            free(shm_cs_buf[thread_num]);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }

        std::cout << "[C++] Client-server shared memory created  (thread "
                  << thread_num << ")" << std::endl;

        pid_t child_status = waitpid(child_pid, NULL, WNOHANG);
        if (child_status != 0)
        {
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            free(shm_sc_buf[thread_num]);
            free(shm_cs_buf[thread_num]);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }

        read_pipe_to_the_end(pipefd_stdout[0], thread_num);
        read_pipe_to_the_end(pipefd_stderr[0], thread_num);

        // Reading XTC file

        int xtc_fd = open(xtc_filename, O_RDONLY);
        if (xtc_fd < 0)
        {
            std::cerr << "[C++] Error in opening the file: " << xtc_filename
                      << " (thread " << thread_num << ")" << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }

        int cfg_fd = -1;
        XtcData::Dgram *dg;

        std::cout << "[C++] Opened " << xtc_filename << " file (thread "
                  << thread_num << ")" << std::endl;

        XtcData::XtcFileIterator iter(xtc_fd, 0x4000000);

        struct message_struct
        {
            long message_type;    /* message type */
            char message_text[1]; /* message text */
        };
        message_struct message_content;
        message_content.message_type = 1;

        message_struct received_message_content;

        unsigned num_event = 0;
        unsigned num_required_events = 100;
        while ((dg = iter.next()))
        {

            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);

            child_status = waitpid(child_pid, NULL, WNOHANG);
            if (child_status != 0)
            {
                msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
                msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
                shmdt(data_array_sc);
                shmdt(data_array_cs);
                shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
                shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
                free(shm_sc_buf[thread_num]);
                free(shm_cs_buf[thread_num]);
                read_pipe_to_the_end(pipefd_stdout[0], thread_num);
                read_pipe_to_the_end(pipefd_stderr[0], thread_num);
                return NULL;
            }

            if (num_event >= num_required_events)
                break;
            num_event++;
            size_t dgram_size = sizeof(*dg) + dg->xtc.sizeofPayload();

            std::cout << "[C++] event " << num_event << "," << std::setw(11)
                      << XtcData::TransitionId::name(dg->service())
                      << " transition: time " << dg->time.seconds()
                      << "." << std::setfill('0') << std::setw(9)
                      << dg->time.nanoseconds()
                      << ", env 0x" << std::setfill('0') << std::setw(8)
                      << dg->env << ", payloadSize " << dg->xtc.sizeofPayload()
                      << " extent " << dg->xtc.extent << " datagram size "
                      << dgram_size << std::endl;

            memcpy(shm_sc_buf[thread_num], dg, dgram_size);

            // uint32_t *source = (uint32_t *)dg;
            // std::cout << "[C++] DEBUG C++ Side - Index: " << num_event << " - "
                    //           << source[0] << " " << source[1] << " " << source[2]
            //           << std::endl;

            int ret_send = -1;
            if (dg->service() == 2)
            {
                std::cout << "[C++] Sending configure transition" << std::endl;
                message_content.message_text[0] = 'c';
            }
            else
            {
                message_content.message_text[0] = 'g';
            }

            ret_send = msgsnd(mq_sc_id[thread_num], (void *)&message_content,
                              sizeof(message_content.message_text), 0);
            if (ret_send == -1)
            {
                std::cerr << "[C++] Error sending message (thread " << thread_num
                          << ")"
                          << " - Errno: " << errno << std::endl;
                msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
                msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
                shmdt(data_array_sc);
                shmdt(data_array_cs);
                shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
                shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
                free(shm_sc_buf[thread_num]);
                free(shm_cs_buf[thread_num]);
                read_pipe_to_the_end(pipefd_stdout[0], thread_num);
                read_pipe_to_the_end(pipefd_stderr[0], thread_num);
                return NULL;
            }

            child_status = waitpid(child_pid, NULL, WNOHANG);
            if (child_status != 0)
            {
                msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
                msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
                shmdt(data_array_sc);
                shmdt(data_array_cs);
                shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
                shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
                free(shm_sc_buf[thread_num]);
                free(shm_cs_buf[thread_num]);
                read_pipe_to_the_end(pipefd_stdout[0], thread_num);
                read_pipe_to_the_end(pipefd_stderr[0], thread_num);
                return NULL;
            }

            int ret_receive = -1;
            int timer = 0;
            time_t start_time = time(NULL);
            while (timer < 5)
            {
                ret_receive = msgrcv(mq_cs_id[thread_num], &received_message_content,
                                     sizeof(received_message_content.message_text),
                                     0, IPC_NOWAIT);
                if (ret_receive != -1)
                {
                    break;
                }

                read_pipe_to_the_end(pipefd_stdout[0], thread_num);
                read_pipe_to_the_end(pipefd_stderr[0], thread_num);

                double difference = (double)(time(NULL) - start_time);
                if (difference > 5)
                {
                    std::cout << "[C++] Message receiving timed out (thread "
                              << thread_num << ")" << std::endl;
                    break;
                }
            }

            if (ret_receive == -1)
            {
                std::cerr << "[C++] Error receiving message (thread " << thread_num
                          << ")" << std::endl;
                msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
                msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
                shmdt(data_array_sc);
                shmdt(data_array_cs);
                shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
                shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
                free(shm_sc_buf[thread_num]);
                free(shm_cs_buf[thread_num]);
                read_pipe_to_the_end(pipefd_stdout[0], thread_num);
                read_pipe_to_the_end(pipefd_stderr[0], thread_num);
                return NULL;
            }
        }

        std::cout << "[C++] Closed " << xtc_filename << " file (thread " << thread_num
                  << ")" << std::endl;

        message_content.message_text[0] = 's';
        int ret_send = msgsnd(mq_sc_id[thread_num], &message_content,
                              sizeof(message_content.message_text), 0);
        if (ret_send == -1)
        {
            std::cerr << "[C++] Error sending message (thread " << thread_num << ")"
                      << " - Errno: " << errno << std::endl;
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmdt(data_array_cs);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
            free(shm_sc_buf[thread_num]);
            free(shm_cs_buf[thread_num]);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }

        child_status = waitpid(child_pid, NULL, WNOHANG);
        if (child_status != 0)
        {
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmdt(data_array_cs);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
            free(shm_sc_buf[thread_num]);
            free(shm_cs_buf[thread_num]);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }

        child_status = waitpid(child_pid, NULL, WNOHANG);
        if (child_status != 0)
        {
            msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
            msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
            shmdt(data_array_sc);
            shmdt(data_array_cs);
            shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
            shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
            free(shm_sc_buf[thread_num]);
            free(shm_cs_buf[thread_num]);
            read_pipe_to_the_end(pipefd_stdout[0], thread_num);
            read_pipe_to_the_end(pipefd_stderr[0], thread_num);
            return NULL;
        }

        // Shut down thread

        std::cout << "[C++] Stopping C++ side "
                  << "(thread " << thread_num << ")" << std::endl;

        msgctl(mq_sc_id[thread_num], IPC_RMID, NULL);
        msgctl(mq_cs_id[thread_num], IPC_RMID, NULL);
        shmdt(data_array_sc);
        shmdt(data_array_cs);
        shmctl(shm_sc_id[thread_num], IPC_RMID, NULL);
        shmctl(shm_cs_id[thread_num], IPC_RMID, NULL);
        free(shm_sc_buf[thread_num]);
        free(shm_cs_buf[thread_num]);

        int status;
        pid_t wait_pid = waitpid(child_pid, &status, 0);

        read_pipe_to_the_end(pipefd_stdout[0], thread_num);
        read_pipe_to_the_end(pipefd_stderr[0], thread_num);

        close(pipefd_stdout[0]);
        close(pipefd_stderr[0]);

        wait_pid == child_pid &&WIFEXITED(status);

        return NULL;
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

    pthread_t first_thread;
    pthread_t second_thread;

    ThreadArgs first_thread_args = ThreadArgs();
    ThreadArgs second_thread_args = ThreadArgs();

    first_thread_args.thread_num = 0;
    second_thread_args.thread_num = 1;
    
    first_thread_args.python_script = argv[1];
    second_thread_args.python_script = argv[1];
    
    first_thread_args.xtc_filename = "/cds/data/psdm/tmo/tmox49720/xtc/smalldata/"
                                     "tmox49720-r0206-s000-c000.smd.xtc2";

    second_thread_args.xtc_filename =  "/cds/data/psdm/tmo/tmox49720/xtc/smalldata/"
                                       "tmox49720-r0206-s003-c000.smd.xtc2";

    pthread_create(&first_thread, NULL,
                   start_server_client, (void *)&first_thread_args);
    pthread_create(&second_thread, NULL,
                   start_server_client, (void *)&second_thread_args);

    pthread_join(first_thread, NULL);
    pthread_join(second_thread, NULL);
    return 0;
}
