#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include <netinet/in.h>

class Interface;
namespace LSA {
class Base;
}

// 路由表，用于计算最短路径
// 这里不再使用linux的路由表，而是自己维护一个路由表
class RoutingTable {
private:
    struct Entry {
        in_addr_t dst;
        in_addr_t mask;
        in_addr_t next_hop; // 若直连，则为0
        uint32_t metric;
        Interface *intf; // 若直连，则为nullptr

        Entry() = default;
        Entry(in_addr_t dst, in_addr_t mask, in_addr_t next_hop, uint32_t metric, Interface *intf)
            : dst(dst), mask(mask), next_hop(next_hop), metric(metric), intf(intf) {
        }
        ~Entry() = default;
    };

    std::list<Entry> routes;

public:
    RoutingTable() {
        // 添加代表自己的根结点
        root_id = ntohl(inet_addr(THIS_ROUTER_ID));
    }

    std::pair<in_addr_t, Interface *> lookup_route(in_addr_t dst, in_addr_t mask) const noexcept;
    void print() const noexcept;
    void debug(std::ostream& os) noexcept;

private:
    struct Node {
        in_addr_t id;
        in_addr_t mask = 0;
        uint32_t dist;
        Node() = default;
        Node(in_addr_t id, uint32_t dist) : id(id), dist(dist) {
        }
        Node(in_addr_t id, in_addr_t mask, uint32_t dist) : id(id), mask(mask), dist(dist) {
        }
        bool operator>(const Node& rhs) const noexcept {
            return dist > rhs.dist;
        }
    };

    struct Edge {
        in_addr_t dst;
        uint32_t metric;
        Edge() = default;
        Edge(in_addr_t dst, uint32_t metric) : dst(dst), metric(metric) {
        }
    };

    uint32_t root_id;

    // 路由器结点和网络结点
    std::unordered_map<in_addr_t, Node> nodes;
    // 每个结点的前驱结点
    std::unordered_map<in_addr_t, in_addr_t> prevs;
    // 每个结点的出边，其中网络结点不应有出边
    std::unordered_map<in_addr_t, std::vector<Edge>> edges;

    void dijkstra() noexcept;

public:
    void update_route() noexcept;
};

extern RoutingTable this_routing_table;