#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>
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

int mq_recv_id;
int mq_send_id;
int shm_recv_id;
int shm_send_id;
void *shm_recv_buf;
void *shm_send_buf;
float* data_array_sc;
float* data_array_cs;

void cleanup()
{
    msgctl(mq_recv_id, IPC_RMID, NULL);
    msgctl(mq_send_id, IPC_RMID, NULL);
    msgctl(mq_recv_id, IPC_RMID, NULL);
    msgctl(mq_send_id, IPC_RMID, NULL);
    shmctl(shm_recv_id, IPC_RMID, NULL);
    shmctl(shm_send_id, IPC_RMID, NULL);
    shmctl(shm_recv_id, IPC_RMID, NULL);
    shmctl(shm_send_id, IPC_RMID, NULL);
    shmdt(data_array_sc);
    shmdt(data_array_cs);
    free(shm_recv_buf);
    free(shm_send_buf);
}

void exit_gracefully(int signum)
{
    std::cout << "Interrupt signal (" << signum << ") received. Cleaning up!\n";
    cleanup();
    exit(signum);
}

int main(int argc, char *argv[])
{

    signal(SIGINT, exit_gracefully);
    // if (argc != 2)
    // {
    //     printf("Usage: drp_xtc_server <xtc file>\n");
    //     exit(1);
    // }

    // std::string xtc_filename = "/cds/data/psdm/tmo/tmox49720/xtc/"
    //                            "tmox49720-r0207-s003-c000.xtc2";
    std::string xtc_filename = "./test_multirun.xtc2";

    std::string output_filename = "./drp_test.xtc2";

    int ofd = open(
        output_filename.c_str(),
        O_WRONLY | O_CREAT,
        S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
    if (ofd < 0)
    {
        std::cerr << "Unable to open file " << output_filename << std::endl;
        exit(2);
    }

    std::cout << "Starting C++ server" << std::endl;

    // Creating message queues

    std::cout << "Creating message queues (thread " << std::endl;
    mq_recv_id = msgget(200000, IPC_CREAT | 0666);
    if (mq_recv_id == -1)
    {
        std::cerr << "Error in creating server-client message queue"
                  << " - Errno: " << errno << std::endl;
        return 0;
    }
    mq_send_id = msgget(200001, IPC_CREAT | 0666);
    if (mq_send_id == -1)
    {
        std::cerr << "Error in creating client-server message queue "
                  << " - Errno: " << errno << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        return 0;
    }

    std::cout << "Message queues created" << std::endl;

    // Creating shared memory

    std::cout << "Creating shared memory blocks" << std::endl;

    unsigned pageSize = (unsigned)sysconf(_SC_PAGESIZE);
    size_t sizeofShm = 3000000;
    unsigned remainder = sizeofShm % pageSize;
    if (remainder != 0)
    {
        sizeofShm += pageSize - remainder;
    }
    void *buffer_sc = NULL;
    int ret = posix_memalign(&buffer_sc, pageSize, sizeofShm);
    if (ret)
    {
        std::cerr << "Error in creating server-client memory buffer (thread"
                  << " - Errno: " << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        msgctl(mq_send_id, IPC_RMID, NULL);
        return 0;
    }
    shm_recv_buf = buffer_sc;
    shm_recv_id = shmget(
        200002,
        sizeofShm,
        IPC_CREAT | IPC_EXCL | 0666);
    if (shm_recv_id == -1)
    {
        std::cerr << "Error in creating server-client shared memory"
                  << " - Errno: " << errno << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        msgctl(mq_send_id, IPC_RMID, NULL);
        return 0;
    }
    data_array_sc = (float *)shmat(shm_recv_id, buffer_sc, SHM_REMAP);
    if (data_array_sc == (void *)-1)
    {
        std::cerr << "Error attaching server-client shared memory (thread"
                  << " - Errno: " << errno << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        msgctl(mq_send_id, IPC_RMID, NULL);
        return 0;
    }

    std::cout << "Server-client shared memory created" << std::endl;

    void *buffer_cs = NULL;
    ret = posix_memalign(&buffer_cs, pageSize, sizeofShm);
    if (ret)
    {
        std::cerr << "Error in creating client-server memory buffer"
                  << " - Errno: " << errno << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        msgctl(mq_send_id, IPC_RMID, NULL);
        shmdt(data_array_sc);
        shmctl(shm_recv_id, IPC_RMID, NULL);
        free(shm_recv_buf);
        return 0;
    }
    shm_send_buf = buffer_cs;
    shm_send_id = shmget(
        200003,
        sizeofShm,
        IPC_CREAT | IPC_EXCL | 0666);
    if (shm_send_id == -1)
    {
        std::cerr << "[C++] Error in creating client-server shared memory (thread "
                  << " - Errno: " << errno << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        msgctl(mq_send_id, IPC_RMID, NULL);
        shmdt(data_array_sc);
        shmctl(shm_recv_id, IPC_RMID, NULL);
        free(shm_recv_buf);
        return 0;
    }
    data_array_cs = (float *)shmat(shm_send_id, buffer_cs, SHM_REMAP);
    if (data_array_cs == (void *)-1)
    {
        std::cerr << "Error in attaching client-server shared memory (thread"
                  << " - Errno: " << errno << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        msgctl(mq_send_id, IPC_RMID, NULL);
        shmdt(data_array_sc);
        shmctl(shm_recv_id, IPC_RMID, NULL);
        free(shm_recv_buf);
        free(shm_send_buf);
        return 0;
    }

    std::cout << "Client-server shared memory created" << std::endl;

    // Reading XTC file

    int xtc_fd = open(xtc_filename.c_str(), O_RDONLY);
    if (xtc_fd < 0)
    {
        std::cerr << "Error in opening the file: " << xtc_filename
                  << std::endl;
        msgctl(mq_recv_id, IPC_RMID, NULL);
        msgctl(mq_send_id, IPC_RMID, NULL);
        return 0;
    }

    int cfg_fd = -1;
    XtcData::Dgram *dg;

    std::cout << "Opened " << xtc_filename << " file" << std::endl;

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
    unsigned num_required_events = 250000;

    std::cout << "Waiting for the python interpreter to start" << std::endl;
    int ret_receive = 1;
    ret_receive = msgrcv(mq_send_id, &received_message_content,
                         sizeof(received_message_content.message_text),
                         0, 0);
    if (ret_receive == -1)
    {
        std::cerr << "[C++] Error receiving message" << std::endl;
        cleanup();
        return -1;
    }

    int end_of_run =0;

    while ((dg = iter.next()))
    {
        if (num_event >= num_required_events)
        {
            break;
        }

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

        memcpy(shm_recv_buf, dg, dgram_size);

        // uint32_t *source = (uint32_t *)dg;
        // std::cout << "[C++] DEBUG C++ Side - Index: " << num_event << " - "
        //           << source[0] << " " << source[1] << " " << source[2]
        //           << std::endl;

        if (dg->service() == 3) {
            std::cout << "End of run, sending stop message" << std::endl;
            message_content.message_text[0] = 's';
            int ret_send = msgsnd(mq_recv_id, &message_content,
                              sizeof(message_content.message_text), 0);
            if (ret_send == -1)
            {
                std::cerr << "Error sending message"
                          << " - Errno: " << errno << std::endl;
                cleanup();
                return -1;
            }
            
            ret_receive = msgrcv(mq_send_id, &received_message_content,
                                 sizeof(received_message_content.message_text),
                                 0, 0);
            if (ret_receive == -1)
            {
                std::cerr << "[C++] Error receiving message" << std::endl;
                cleanup();
                return -1;
            }
        } else {
            int ret_send = -1;
            message_content.message_text[0] = 'g';
            ret_send = msgsnd(mq_recv_id, (void *)&message_content,
                              sizeof(message_content.message_text), 0);
            if (ret_send == -1)
            {
                std::cerr << "[C++] Error sending message"
                          << " - Errno: " << errno << std::endl;
                cleanup();
                return -1;
            }

            ret_receive = -1;
            ret_receive = msgrcv(mq_send_id, &received_message_content,
                                 sizeof(received_message_content.message_text),
                                 0, 0);
            if (ret_receive == -1)
            {
                std::cerr << "[C++] Error receiving message" << std::endl;
                cleanup();
                return -1;
            }

            if (received_message_content.message_text[0] == 'c')
            {
                continue;
            }

            XtcData::Dgram *out_dg = (XtcData::Dgram *)shm_send_buf;
            std::cout << "Received modified event: "
                      << XtcData::TransitionId::name(out_dg->service()) << " - Timestamp: "
                      << " transition: time " << out_dg->time.seconds()
                      << "." << std::setfill('0') << std::setw(9)
                      << out_dg->time.nanoseconds() << " - Datagram size: "
                      << sizeof(*out_dg) + out_dg->xtc.sizeofPayload()
                      << std::endl;

            if (write(ofd, out_dg, sizeof(*out_dg) + out_dg->xtc.sizeofPayload()) < 0)
            {
                std::cerr << "Error writing to output xtc file." << std::endl;
                cleanup();
                return -1;
            }

            if (dg->service() == 3) {
                std::cout << "End of run, sending stop message" << std::endl;
                message_content.message_text[0] = 's';
                int ret_send = msgsnd(mq_recv_id, &message_content,
                                  sizeof(message_content.message_text), 0);
                if (ret_send == -1)
                {
                    std::cerr << "Error sending message"
                              << " - Errno: " << errno << std::endl;
                    cleanup();
                    return -1;
                }
            }
        } 
    }

    std::cout << "Closed " << xtc_filename << " file" << std::endl;
    close(ofd);

    std::cout << "Stopping C++ side " << std::endl;
    cleanup();
    return 0;
}
