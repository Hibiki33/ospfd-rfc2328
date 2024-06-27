#include <cassert>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <queue>

#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "interface.hpp"
#include "lsdb.hpp"
#include "neighbor.hpp"
#include "packet.hpp"
#include "route.hpp"
#include "utils.hpp"

RoutingTable this_routing_table;

// 查找路由表
// 返回下一跳地址和接口
std::pair<in_addr_t, Interface *> RoutingTable::lookup_route(in_addr_t dst) const noexcept {
    for (auto& route : routes) {
        if ((dst & route.mask) == route.dst) {
            return {route.next_hop, route.intf};
        }
    }
    return {0, nullptr};
}

// 打印路由表
void RoutingTable::print() const noexcept {
    std::cout << "Routing Table:" << std::endl;
    std::cout << std::left                                  // 幽默stream
              << std::setw(15) << "Destination"             //
              << std::setw(15) << "Mask"                    //
              << std::setw(15) << "Next Hop"                //
              << std::setw(15) << "Metric"                  //
              << std::setw(15) << "Interface" << std::endl; //
    for (auto& route : routes) {
        std::cout << std::left                                      //
                  << std::setw(15) << ip_to_str(route.dst)          //
                  << std::setw(15) << ip_to_str(route.mask)         //
                  << std::setw(15) << ip_to_str(route.next_hop)     //
                  << std::setw(15) << route.metric << std::setw(15) //
                  << (route.intf ? route.intf->name : "direct") << std::endl;
    }
}

void RoutingTable::debug(std::ostream& os) noexcept {
    // 打印路由表
    os << "Routing Table:" << std::endl;
    os << std::left                                  //
       << std::setw(15) << "Destination"             //
       << std::setw(15) << "Mask"                    //
       << std::setw(15) << "Next Hop"                //
       << std::setw(15) << "Metric"                  //
       << std::setw(15) << "Interface" << std::endl; //
    for (auto& route : routes) {
        os << std::left                                      //
           << std::setw(15) << ip_to_str(route.dst)          //
           << std::setw(15) << ip_to_str(route.mask)         //
           << std::setw(15) << ip_to_str(route.next_hop)     //
           << std::setw(15) << route.metric << std::setw(15) //
           << (route.intf ? route.intf->name : "direct") << std::endl;
    }

    // 打印拓扑
    os << "Topology:" << std::endl;
    for (auto& node : nodes) {
        if (node.second.mask != 0) {
            continue;
        }
        os << ip_to_str(node.second.id) << " -> " << std::endl;
        for (auto& edge : edges[node.second.id]) {
            os << "\t" << ip_to_str(edge.dst) << "(" << edge.metric << ") " << std::endl;
        }
    }

    // 打印路径
    os << "Paths:" << std::endl;
    for (auto& node : nodes) {
        if (node.second.mask == 0) {
            continue;
        }
        in_addr_t prev = node.second.id;
        while (prev != root_id) {
            os << ip_to_str(prev) << " <- ";
            prev = prevs[prev];
        }
        os << ip_to_str(root_id) << std::endl;
    }
}

void RoutingTable::update_route() noexcept {
    std::cout << "Updating route..." << std::endl;
    nodes.clear();
    prevs.clear();
    edges.clear();
    reset_kernel_route();

    // 从第一类和第二类LSA中记录结点信息
    this_lsdb.lock();
    for (auto& lsa : this_lsdb.router_lsas) {
        // 对路由器结点，ls_id为其路由器id
        nodes[lsa->header.link_state_id] = {lsa->header.link_state_id, UINT32_MAX};
        // 记录路由器结点的出边
        for (auto& link : lsa->links) {
            if (link.type == LSA::LinkType::POINT2POINT) {
                // 对点到点网络，link_id为对端路由器id
                Edge edge(link.link_id, link.metric);
                edges[lsa->header.link_state_id].push_back(edge);
            } else if (link.type == LSA::LinkType::TRANSIT) {
                // 对中转网络，link_id为该网络dr的接口ip
                // 因此需要查Network LSA找到所有对应的网络结点
                auto nlsa = this_lsdb.get_network_lsa(link.link_id);
                for (auto& router_id : nlsa->attached_routers) {
                    if (router_id == lsa->header.link_state_id) {
                        continue;
                    }
                    Edge edge(router_id, link.metric);
                    edges[lsa->header.link_state_id].push_back(edge);
                }
            } else if (link.type == LSA::LinkType::STUB) {
                // 对stub网络，link_id为网络ip，link_data为mask
                // 需要新建一个结点
                nodes[link.link_id] = {link.link_id, link.link_data, UINT32_MAX};
                Edge edge(link.link_id, link.metric);
                edges[lsa->header.link_state_id].push_back(edge);
            } else {
                // TODO: 暂时不考虑虚拟链路
            }
        }
    }
    for (auto& lsa : this_lsdb.network_lsas) {
        // 对网络结点，id本来是dr的接口ip，可能与路由器id相同
        // 因此这里将网络结点的id按位与其mask，并用mask区分是否是网络结点
        auto net_node_id = lsa->header.link_state_id & lsa->network_mask;
        nodes[net_node_id] = {net_node_id, lsa->network_mask, UINT32_MAX};
        // 路由器到网络结点的入边，距离为0
        for (auto& router_id : lsa->attached_routers) {
            Edge edge(net_node_id, 0);
            edges[router_id].push_back(edge);
        }
    }
    this_lsdb.unlock();

    // 执行dijkstra算法
    dijkstra();

    // 3-5 LSA
    this_lsdb.lock();
    // 构造区域间路由
    for (auto& lsa : this_lsdb.summary_lsas) {
        // 如果是自己的LSA
        if (lsa->header.advertising_router == ntohl(inet_addr(THIS_ROUTER_ID))) {
            continue;
        }
        auto it = nodes.find(lsa->header.advertising_router);
        // 如果不存在
        if (it == nodes.end()) {
            continue;
        }
        auto abr_node = it->second;
        // 如果不可达
        if (abr_node.dist == UINT32_MAX) {
            continue;
        }
        // 路径长度不需要加lsa的metric
        Node net_node(lsa->header.link_state_id, lsa->network_mask, nodes[abr_node.id].dist); // + lsa->metric);
        nodes[net_node.id] = net_node;
        Edge edge(net_node.id, lsa->metric);
        edges[abr_node.id].push_back(edge);
        prevs[net_node.id] = abr_node.id;
    }
    // TODO: 构造外部路由
    this_lsdb.unlock();

    // 将结点信息写入路由表
    routes.clear();
    for (auto& pair : nodes) {
        auto& node = pair.second;

        // 路由表只关心网络结点和存根网络
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
            for (auto& intf : this_interfaces) {
                auto nbr = intf->get_neighbor_by_id(next_id);
                if (nbr) {
                    next_hop = nbr->ip_addr;
                    interface = intf;
                    break;
                }
            }
        } else {
            next_hop = 0;
            for (auto& intf : this_interfaces) {
                if (dst == (intf->ip_addr & intf->mask)) {
                    interface = intf;
                    break;
                }
            }
        }

        // 无论是直连还是间接，都要有接口
        assert(interface != nullptr);

        Entry entry(dst, mask, next_hop, metric, interface);
        routes.push_back(entry);
    }

    update_kernel_route();
    std::cout << "Update route done." << std::endl;
}

void RoutingTable::dijkstra() noexcept {
    auto heap = std::priority_queue<Node, std::vector<Node>, std::greater<Node>>();
    std::unordered_map<in_addr_t, bool> vis;

    // 初始化前驱结点和访问标记
    for (auto& node : nodes) {
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

        for (auto& edge : edges[node.id]) {
            if (nodes[edge.dst].dist > node.dist + edge.metric) {
                nodes[edge.dst].dist = node.dist + edge.metric;
                prevs[edge.dst] = node.id;
                heap.push(nodes[edge.dst]);
            }
        }
    }
}

void RoutingTable::update_kernel_route() {
    for (auto& entry : routes) {
        rtentry rtentry;
        memset(&rtentry, 0, sizeof(rtentry));

        // 设置
        rtentry.rt_dst.sa_family = AF_INET;
        ((sockaddr_in *)&rtentry.rt_dst)->sin_addr.s_addr = htonl(entry.dst);
        rtentry.rt_genmask.sa_family = AF_INET;
        ((sockaddr_in *)&rtentry.rt_genmask)->sin_addr.s_addr = htonl(entry.mask);
        rtentry.rt_gateway.sa_family = AF_INET;
        ((sockaddr_in *)&rtentry.rt_gateway)->sin_addr.s_addr = htonl(entry.next_hop);
        rtentry.rt_metric = entry.metric;
        // 如果是直连
        if (entry.next_hop == 0) {
            rtentry.rt_flags = RTF_UP;
        } else {
            rtentry.rt_flags = RTF_UP | RTF_GATEWAY;
        }
        rtentry.rt_dev = entry.intf->name;

        // 写入
        if (ioctl(kernel_route_fd, SIOCADDRT, &rtentry) < 0) {
            perror("write kernel route failed");
        }

        // 备份
        kernel_routes.push_back(rtentry);
    }
}

void RoutingTable::reset_kernel_route() {
    for (auto& rtentry : kernel_routes) {
        // 如果直连，不删除
        if (!(rtentry.rt_flags & RTF_GATEWAY)) {
            continue;
        }

        if (ioctl(kernel_route_fd, SIOCDELRT, &rtentry) < 0) {
            perror("remove kernel route failed");
        }
    }
    kernel_routes.clear();
}