#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "interface.hpp"
#include "packet.hpp"
#include "transit.hpp"

void ospf_daemon();
void ospf_normal();

int main(int argc, char *argv[]) {
    // parse args
    bool daemon = false;
    if (argc > 1 && (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--daemon") == 0)) {
        daemon = true;
    }

    // init interfaces
    this_interfaces.emplace_back(new Interface((inet_addr(ETH0_IP)), ntohl(inet_addr(ETH0_MASK))));
    for (auto& intf : this_interfaces) {
        intf->event_interface_up();
        std::cout << "Interface " << intf->ip_addr << " up." << std::endl;
    }

    if (daemon) {
        // run as daemon
        std::thread daemon_thread(ospf_daemon);
        daemon_thread.detach();
    } else {
        // run normally
        ospf_normal();
    }

    return 0;
}

void ospf_daemon() {
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // it's child (daemon) process now
    umask(0);

    pid_t sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) {
        exit(EXIT_FAILURE);
    }

    int log_fd = open("/tmp/ospf_daemon.log", O_RDWR | O_CREAT | O_APPEND,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (log_fd < 0) {
        exit(EXIT_FAILURE);
    }

    if (dup2(log_fd, STDOUT_FILENO) < 0) {
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    if (dup2(log_fd, STDERR_FILENO) < 0) {
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    close(log_fd);
    std::cout << "OSPF daemon started." << std::endl;

    close(STDIN_FILENO);

    ospf_normal();

    std::cout << "OSPF daemon stopped." << std::endl;
}

void ospf_normal() {
    std::cout << "OSPF send/recv started." << std::endl;

    OSPF::running = true;
    std::thread send_thread(OSPF::send_loop);
    std::thread recv_thread(OSPF::recv_loop);

    while (true) {
        std::string cmd;
        std::cin >> cmd;
        if (cmd == "exit") {
            OSPF::running = false;
            break;
        }
    }

    send_thread.join();
    recv_thread.join();

    std::cout << "OSPF send/recv stopped." << std::endl;
}
