#include "routing.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <sys/ioctl.h>
#include <unistd.h>

void RoutingTable::reset_kernel_route() {
    int ret;
    for (auto& entry : written_rtentries) {
        ret = ioctl(routefd, SIOCDELRT, &entry);
        if (ret < 0) {
            sockaddr_in dest = *(sockaddr_in *)&entry.rt_dst;
            printf("delete route %s", inet_ntoa(dest.sin_addr));
            perror(":");
        }
    }
    written_rtentries.clear();
}

void RoutingTable::write_kernel_route() {
    reset_kernel_route();

    int ret;
    for (auto& route_it : routings) {
        auto entry = &route_it.second;
        rtentry rte;
        memset(&rte, 0, sizeof(rte));

        rte.rt_dst.sa_family = AF_INET;
        ((sockaddr_in *)&rte.rt_dst)->sin_addr.s_addr = htonl(entry->dst);
        rte.rt_genmask.sa_family = AF_INET;
        ((sockaddr_in *)&rte.rt_genmask)->sin_addr.s_addr = htonl(entry->mask);
        rte.rt_gateway.sa_family = AF_INET;
        ((sockaddr_in *)&rte.rt_gateway)->sin_addr.s_addr = htonl(entry->next_hop);
        rte.rt_metric = htons(entry->metric);
        rte.rt_flags = RTF_UP | RTF_GATEWAY;

        if ((ret = ioctl(routefd, SIOCADDRT, &rte)) < 0) {
            perror("add route:");
        } else {
            written_rtentries.emplace_back(rte);
        }
    }
}