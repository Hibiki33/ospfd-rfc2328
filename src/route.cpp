#include <cassert>
#include <cstring>
#include <iostream>
#include <queue>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/ioctl.h>

#include "interface.hpp"
#include "lsdb.hpp"
#include "neighbor.hpp"
#include "packet.hpp"
#include "route.hpp"
#include "utils.hpp"

RoutingTable this_routing_table;

// 查找路由表
// 返回下一跳地址和接口
std::pair<in_addr_t, Interface *> RoutingTable::lookup_route(in_addr_t dst, in_addr_t mask) const noexcept {
    for (const auto& route : routes) {
        if ((dst & route.mask) == route.dst) {
            return {route.next_hop, route.intf};
        }
    }
    return {0, nullptr};
}

// 打印路由表
void RoutingTable::print() const noexcept {
    printf("%-15s%-15s%-15s%-15s%-15s\n", "Destination", "Mask", "Next Hop", "Metric", "Interface");
    for (const auto& route : routes) {
        printf("%-15s%-15s%-15s%-15u%-15s\n", ip_to_str(route.dst).data(), ip_to_str(route.mask).data(),
               ip_to_str(route.next_hop).data(), route.metric,
               strlen(route.intf->name) > 0 ? route.intf->name : ip_to_str(route.intf->ip_addr).data());
    }
}

void RoutingTable::debug(std::ostream& os) const noexcept {
    os << "routing table:" << std::endl;
    for (const auto& route : routes) {
        os << ip_to_str(route.dst) << "/" << mask_to_num(route.mask) << " via " << ip_to_str(route.next_hop)
           << " metric " << route.metric << " on " << route.intf->name << std::endl;
    }
}

void RoutingTable::update_route() noexcept {
    nodes.clear();
    prevs.clear();
    edges.clear();

    // 从第一类和第二类LSA中记录结点信息
    this_lsdb.lock();
    for (const auto& lsa : this_lsdb.router_lsas) {
        // 对路由器结点，id为其路由器id
        nodes[lsa->header.link_state_id] = {lsa->header.link_state_id, UINT32_MAX};
        // 记录路由器结点的出边
        for (const auto& link : lsa->links) {
            if (link.type == LSA::LinkType::POINT2POINT) {
                // 对点到点网络，link_id为对端路由器id
                edges[lsa->header.link_state_id].emplace_back(link.link_id, link.metric);
            } else if (link.type == LSA::LinkType::TRANSIT) {
                // 对中转网络，link_id为该网络dr的接口ip
                // 因此需要查Network LSA找到所有对应的网络结点
                for (const auto& network_lsa : this_lsdb.network_lsas) {
                    auto nlsa = this_lsdb.get_network_lsa(link.link_id);
                    for (const auto& router_id : nlsa->attached_routers) {
                        if (router_id == link.link_data) {
                            continue;
                        }
                        edges[lsa->header.link_state_id].emplace_back(router_id, link.metric);
                    }
                }
            } else {
                // TODO: 暂时不考虑存根和虚拟链路
            }
        }
    }
    for (const auto& lsa : this_lsdb.network_lsas) {
        // 对网络结点，id本来是dr的接口ip，可能与路由器id相同
        // 因此这里将网络结点的id按位与其mask，并用mask区分是否是网络结点
        auto net_node_id = lsa->header.link_state_id & lsa->network_mask;
        nodes[net_node_id] = {net_node_id, lsa->network_mask, UINT32_MAX};
        // 路由器到网络结点的入边，距离为0
        for (const auto& router_id : lsa->attached_routers) {
            edges[router_id].emplace_back(net_node_id, .0);
        }
    }
    this_lsdb.unlock();

    // 执行dijkstra算法
    dijkstra();

    // TODO: 暂时不考虑3-5类LSA

    // 将结点信息写入路由表
    routes.clear();
    for (const auto& pair : nodes) {
        auto& node = pair.second;

        // 路由表只关心网络结点
        if (node.mask == 0) {
            continue;
        }
        in_addr_t dst = node.id;
        in_addr_t mask = node.mask;
        in_addr_t next_hop = 0;
        uint32_t metric = node.dist;
        Interface *interface = nullptr;

        // 查找下一跳的地址和自身接口
        if (prevs[dst] != root_id) {
            in_addr_t prev_hop = prevs[dst];
            in_addr_t next_id = dst;
            while (prev_hop != root_id) {
                next_id = prev_hop;
                prev_hop = prevs[prev_hop];
            }
            // 查邻居对应的接口
            for (const auto& intf : this_interfaces) {
                for (const auto& nbr : intf->neighbors) {
                    if (nbr->id == next_id) {
                        next_hop = nbr->ip_addr;
                        break;
                    }
                }
                interface = intf;
            }
        }

        routes.emplace_back(dst, mask, next_hop, metric, interface);
    }
}

void RoutingTable::dijkstra() noexcept {
    auto heap = std::priority_queue<Node, std::vector<Node>, std::greater<Node>>();
    std::unordered_map<in_addr_t, bool> vis;

    // 初始化前驱结点和访问标记
    for (const auto& node : nodes) {
        prevs[node.first] = 0;
        vis[node.first] = false;
    }

    // 初始化根节点
    nodes[root_id].dist = 0;
    heap.push(nodes[root_id]);

    // 计算最短路径
    while (!heap.empty()) {
        auto node = heap.top();
        heap.pop();
        if (vis[node.id]) {
            continue;
        }
        vis[node.id] = true;

        for (const auto& edge : edges[node.id]) {
            if (nodes[edge.dst].dist > node.dist + edge.metric) {
                nodes[edge.dst].dist = node.dist + edge.metric;
                prevs[edge.dst] = node.id;
                heap.push(nodes[edge.dst]);
            }
        }
    }
}