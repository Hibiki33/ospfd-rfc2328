#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "interface.hpp"
#include "packet.hpp"
#include "route.hpp"
#include "transit.hpp"
#include "utils.hpp"

void ospf_daemon();
void ospf_run();

int main(int argc, char *argv[]) {
    // parse args
    bool daemon = false;
    if (argc > 1 && (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--daemon") == 0)) {
        daemon = true;
    }

    // init interfaces
    init_interfaces();

    if (daemon) {
        // run as daemon
        std::thread daemon_thread(ospf_daemon);
        daemon_thread.detach();
    } else {
        // run normally
        ospf_run();
    }

    // turn off promisc mode
    for (auto intf : this_interfaces) {
        ifreq ifr;
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        strncpy(ifr.ifr_name, intf->name, IFNAMSIZ);
        ioctl(fd, SIOCGIFFLAGS, &ifr);
        ifr.ifr_flags &= ~IFF_PROMISC;
        ioctl(fd, SIOCSIFFLAGS, &ifr);
        close(fd);
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

    int log_fd = open("/tmp/ospf_daemon.log", O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
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

    ospf_run();

    std::cout << "OSPF daemon stopped." << std::endl;
}

void ospf_run() {
    std::cout << "OSPF send/recv started." << std::endl;

    OSPF::running = true;

    // alloc recv fd
    if ((OSPF::recv_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP))) < 0) {
        perror("recv socket_fd init");
    }

    std::thread send_thread(OSPF::send_loop);
    std::thread recv_thread(OSPF::recv_loop);

    while (true) {
        std::string cmd;
        std::cin >> cmd;
        if (cmd == "exit") {
            OSPF::running = false;
            break;
        }
        if (cmd == "debug") {
            std::ofstream debug_log("./debug.log", std::ios::trunc);
            this_routing_table.debug(debug_log);
            this_routing_table.debug(std::cout);
        }
    }

    send_thread.join();
    recv_thread.join();

    std::cout << "OSPF send/recv stopped." << std::endl;
}