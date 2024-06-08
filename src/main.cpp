#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "interface.hpp"
#include "packet.hpp"
#include "transit.hpp"
#include "utils.hpp"

void init_interfaces();
void ospf_daemon();
void ospf_normal();

int main(int argc, char *argv[]) {
    // parse args
    bool daemon = false;
    if (argc > 1 && (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "--daemon") == 0)) {
        daemon = true;
    }

    // init interfaces
    init_interfaces();
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
    exit(0);

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

void init_interfaces() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        exit(EXIT_FAILURE);
    }

    const int MAX_IF = MAX_INTERFACE_NUM;
    ifreq ifr[MAX_IF];

    ifconf ifc;
    ifc.ifc_len = sizeof(ifr);
    ifc.ifc_req = ifr;
    if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
        close(fd);
        exit(EXIT_FAILURE);
    }

    int num_ifr = ifc.ifc_len / sizeof(ifreq);

    for (auto i = 0; i < num_ifr; ++i) {
        ifreq *ifr = &ifc.ifc_req[i];
        if (strcmp(ifr->ifr_name, "lo") == 0) {
            continue;
        }

        // fetch interface name, ip addr, mask
        auto intf = new Interface();
        strncpy(intf->name, ifr->ifr_name, IFNAMSIZ);
        if (ioctl(fd, SIOCGIFADDR, ifr) < 0) {
            perror("ioctl SIOCGIFADDR");
            delete intf;
            continue;
        }
        intf->ip_addr = ntohl(((sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr);
        if (ioctl(fd, SIOCGIFNETMASK, ifr) < 0) {
            perror("ioctl SIOCGIFNETMASK");
            delete intf;
            continue;
        }
        intf->mask = ntohl(((sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr);

        // turn on promisc mode
        if (ioctl(fd, SIOCGIFFLAGS, ifr) < 0) {
            perror("ioctl SIOCGIFFLAGS");
            delete intf;
            continue;
        }
        ifr->ifr_flags |= IFF_PROMISC;
        if (ioctl(fd, SIOCSIFFLAGS, ifr) < 0) {
            perror("ioctl SIOCSIFFLAGS");
            delete intf;
            continue;
        }

        // add to interfaces
        this_interfaces.push_back(intf);
    }

    close(fd);

    std::cout << "Found " << this_interfaces.size() << " interfaces." << std::endl;
    for (auto intf : this_interfaces) {
        std::cout << "Interface " << intf->name << ":" << std::endl
                  << "\tip addr:" << ip_to_string(intf->ip_addr) << std::endl
                  << "\tmask:" << ip_to_string(intf->mask) << std::endl;
        intf->area_id = 0;
        intf->hello_timer = 0;
        intf->wait_timer = 0;
        intf->event_interface_up();
    }
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
