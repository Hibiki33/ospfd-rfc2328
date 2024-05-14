#pragma once

#include <cstdint>
#include <iostream>
#include <linux/route.h>
#include <netinet/in.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

class RoutingTable {
public:
    struct Entry {
        in_addr_t dst;
        in_addr_t mask;
        in_addr_t next_hop;
        uint32_t metric;
        uint8_t type;
    };

    std::unordered_map<in_addr_t, Entry> routings;

public:
    RoutingTable() noexcept {
        routefd = socket(AF_INET, SOCK_DGRAM, 0);
        if (routefd < 0) {
            perror("init routefd");
            exit(-1);
        }
    }
    ~RoutingTable() {
        reset_kernel_route();
        close(routefd);
    }

private:
    /* manage linux kernel routing table */
    int routefd;                            // socket fd for kernel route
    std::vector<rtentry> written_rtentries; // written rtentries
    void reset_kernel_route();
    void write_kernel_route();
};