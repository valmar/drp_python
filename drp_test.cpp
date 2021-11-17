#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include <thread>
#include <stdio.h>
#include "zmq.hpp"

int start_server_client(zmq::context_t *zmq_context, int thread_num, char *argv[])
{

    int child_pid = fork();

    if (child_pid == 0)
    {
        std::stringstream file_name;
        std::cout << "Starting python client " << thread_num << std::endl;

        int script_argc = 2;
        wchar_t *script_argv[2];
        script_argv[0] = Py_DecodeLocale(argv[1], NULL);
        script_argv[1] = Py_DecodeLocale(std::to_string(thread_num).c_str(), NULL);

        wchar_t *program = Py_DecodeLocale(file_name.str().c_str(), NULL);
        if (program == NULL)
        {
            std::cerr << "Fatal error: cannot decode argv[0]" << std::endl;
            return 1;
        }
        Py_SetProgramName(program);

        Py_Initialize();

        std::cout << "Launching " << argv[1] << std::endl;
        FILE *fptr = fopen(argv[1], "r");
        if (fptr == NULL)
        {
            std::cerr << "Fatal error: cannot find file " << file_name.str() << std::endl;
            return 1;
        }

        PySys_SetArgv(script_argc, script_argv);
        int returned = PyRun_SimpleFileEx(fptr, file_name.str().c_str(), 1);
        if (returned != 0)
        {
            std::cout << "Oh no, an error occurred! I am dying!" << std::endl;
        }

        if (Py_FinalizeEx() < 0)
        {
            return 1;
        }
        PyMem_RawFree(program);
        PyMem_RawFree(script_argv[0]);
        PyMem_RawFree(script_argv[1]);
        return 0;
    }
    else
    {
        // std::cout << "Starting Python client " << thread_num << std::endl;

        // std::stringstream python_command;
        // python_command << "python " << argv[1] << " " << thread_num << std::endl;
        // std::cout << python_command.str() << std::endl;

        // int ret = subprocess::Popen("python -V", subprocess::shell(true)).wait();
        // // FILE pipe = popen(python_command.str().c_str(), "r");
        // // if (!pipe)
        // // {
        // //     std::cerr << "Couldn't start Python client." << std::endl;
        // //     return 0;
        // // }

        std::cout << "Starting C++ server " << thread_num << std::endl;
        zmq::socket_t socket(*zmq_context, zmq::socket_type::pair);

        std::stringstream socket_name;
        socket_name << "ipc:///tmp/drpsocket" << thread_num;
        socket.bind(socket_name.str().c_str());

        for (int i = 0; i < 10; i++)
        {
            zmq::message_t request;

            socket.recv(request, zmq::recv_flags::none);
            std::cout << "Received Hello from client " << thread_num << std::endl;

            sleep(1);

            zmq::message_t reply(5);
            memcpy(reply.data(), "World", 5);
            socket.send(reply, zmq::send_flags::none);
        }
        return 0;
    }
}

int main(int argc, char *argv[])
{
    zmq::context_t context(2);
    if (argc != 2)
    {
        printf("Usage: drp_test <python file>\n");
        exit(1);
    }
    std::thread first(start_server_client, &context, 0, argv);
    std::thread second(start_server_client, &context, 1, argv);
    first.join();
    second.join();
    return 0;
}
