#include <algorithm>
#include <arpa/inet.h>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "interface.hpp"
#include "neighbor.hpp"
#include "transit.hpp"

namespace OSPF {

std::atomic<bool> running;
int recv_fd;

static void recv_process_hello(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_hello = reinterpret_cast<OSPF::Hello *>(ospf_packet + sizeof(OSPF::Header));
    // auto nbr = intf->get_neighbor(src_ip);
    // if (nbr == nullptr) {
    //     nbr = intf->add_neighbor(src_ip);
    // }
    Neighbor *nbr = intf->get_neighbor_by_ip(src_ip);
    if (nbr == nullptr) {
        nbr = new Neighbor(src_ip, intf);
        intf->neighbors.push_back(nbr);
    }

    nbr->id = ospf_hdr->router_id; // hdr已经是host字节序
    auto prev_ndr = nbr->designated_router;
    auto prev_nbdr = nbr->backup_designated_router;
    nbr->designated_router = ntohl(ospf_hello->designated_router);
    nbr->backup_designated_router = ntohl(ospf_hello->backup_designated_router);
    nbr->priority = ntohl(ospf_hello->router_priority);

    nbr->event_hello_received();

    auto to_2way = false;
    // 1way/2way: hello报文中的neighbors列表中是否包含自己
    in_addr_t *attached_nbr = ospf_hello->neighbors;
    while (attached_nbr != reinterpret_cast<in_addr_t *>(ospf_packet + ospf_hdr->length)) {
        if (*attached_nbr == inet_addr(THIS_ROUTER_ID)) {
            to_2way = true;
            break;
        }
        attached_nbr++;
    }
    if (to_2way) {
        // 邻居的Hello报文中包含自己，触发2way事件
        // 如果在这里需要建立邻接，邻接会直接进入exstart状态
        // 否则会进入并维持在2way状态，等待adj_ok事件
        nbr->event_2way_received();
    } else {
        if (nbr->state != Neighbor::State::INIT) {
            nbr->event_1way_received();
        }
        return;
    }

    if (nbr->designated_router == nbr->ip_addr && nbr->backup_designated_router == 0 &&
        intf->state == Interface::State::WAITING) {
        // 如果邻居宣称自己是DR，且自己不是BDR
        intf->event_backup_seen();
    } else if ((prev_ndr == nbr->ip_addr) ^ (nbr->designated_router == nbr->ip_addr)) {
        intf->event_neighbor_change();
    }
    if (nbr->backup_designated_router == nbr->ip_addr && intf->state == Interface::State::WAITING) {
        // 如果邻居宣称自己是BDR
        intf->event_backup_seen();
    } else if ((prev_nbdr == nbr->ip_addr) ^ (nbr->backup_designated_router == nbr->ip_addr)) {
        intf->event_neighbor_change();
    }
}

static void recv_process_dd(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_dd = reinterpret_cast<OSPF::DD *>(ospf_packet + sizeof(OSPF::Header));
    // auto nbr = intf->get_neighbor(src_ip);
}

void recv_loop() {
    iphdr *ip_hdr;
    Interface *intf;
    char recv_frame[ETH_FRAME_LEN];
    auto recv_packet = recv_frame + sizeof(ethhdr);
    while (running) {
        memset(recv_frame, 0, ETH_FRAME_LEN);
        // auto recv_size = recv(recv_fd, recv_frame, ETH_FRAME_LEN, 0);
        auto recv_size = recvfrom(recv_fd, recv_frame, ETH_FRAME_LEN, 0, nullptr, nullptr);
        if (recv_size < sizeof(iphdr)) {
            perror("recv_loop: not receive enough data");
        }

        // 解析IP头部
        ip_hdr = reinterpret_cast<iphdr *>(recv_packet);
        // 如果不是OSPF协议的数据包
        if (ip_hdr->protocol != IPPROTO_OSPF) {
            continue;
        }
        // 如果源地址或目的地址不匹配
        auto src_ip = ntohl(ip_hdr->saddr);
        auto dst_ip = ntohl(ip_hdr->daddr);

        auto ospf_hdr = reinterpret_cast<OSPF::Header *>(recv_packet + sizeof(iphdr));
        ospf_hdr->network_to_host();

        // 如果是本机发送的数据包
        if (ospf_hdr->router_id == ntohl(inet_addr(THIS_ROUTER_ID))) {
            continue;
        }

        // 查找接口
        auto intf_it = std::find_if(this_interfaces.begin(), this_interfaces.end(), [dst_ip](Interface *intf) {
            return dst_ip == intf->ip_addr || dst_ip == ntohl(inet_addr(ALL_SPF_ROUTERS));
        });
        if (intf_it == this_interfaces.end()) {
            continue;
        }
        intf = *intf_it;

        switch (ospf_hdr->type) {
        case OSPF::Type::HELLO:
            recv_process_hello(intf, reinterpret_cast<char *>(ospf_hdr), src_ip);
            break;
        case OSPF::Type::DD:
            break;
        case OSPF::Type::LSR:
            break;
        case OSPF::Type::LSU:
            break;
        case OSPF::Type::LSACK:
            break;
        default:
            break;
        }
    }
}

// 发送IP包，包含OSPF报文
static void send_packet(Interface *intf, char *packet, size_t len, OSPF::Type type, in_addr_t dst) {
    // 构造目标地址
    sockaddr_in dst_sockaddr;
    memset(&dst_sockaddr, 0, sizeof(dst_sockaddr));
    dst_sockaddr.sin_family = AF_INET;
    dst_sockaddr.sin_addr.s_addr = htonl(dst);

    // 构造发送数据包
    auto packet_len = sizeof(OSPF::Header) + len;

    // 构造OSPF头部
    auto ospf_header = reinterpret_cast<OSPF::Header *>(packet);
    ospf_header->version = OSPF_VERSION;
    ospf_header->type = type;
    ospf_header->length = packet_len;
    ospf_header->router_id = ntohl(inet_addr(THIS_ROUTER_ID));
    ospf_header->area_id = intf->area_id;
    ospf_header->checksum = 0;
    ospf_header->auth_type = 0;
    ospf_header->auth = 0;
    ospf_header->host_to_network();

    // 计算校验和
    // 这里不需要转换为网络字节序，因为本来就是按网络字节序计算的
    ospf_header->checksum = crc_checksum(packet, packet_len);

    // 发送数据包
    if (sendto(intf->send_fd, packet, packet_len, 0, reinterpret_cast<sockaddr *>(&dst_sockaddr),
               sizeof(dst_sockaddr)) < 0) {
        perror("send_packet: sendto");
    }
}

static size_t send_produce_hello(Interface *intf, char *body) {
    auto hello = reinterpret_cast<OSPF::Hello *>(body);
    hello->network_mask = intf->mask;
    hello->hello_interval = intf->hello_interval;
    hello->options = 0x02;
    hello->router_priority = intf->router_priority;
    hello->router_dead_interval = intf->router_dead_interval;
    hello->designated_router = intf->designated_router;
    hello->backup_designated_router = intf->backup_designated_router;

    auto attached_nbr = hello->neighbors;
    for (auto& nbr : intf->neighbors) {
        *attached_nbr = nbr->ip_addr;
        attached_nbr++;
    }

    hello->host_to_network(intf->neighbors.size());

    return sizeof(OSPF::Hello) + sizeof(in_addr_t) * intf->neighbors.size();
}

static size_t send_produce_dd(Interface *intf, char *body, Neighbor *nbr) {
    return 0;
}

void send_loop() {
    char data[ETH_DATA_LEN];
    while (running) {
        for (auto& intf : this_interfaces) {
            if (intf->state == Interface::State::DOWN) {
                continue;
            }

            // Hello packet
            if ((++intf->hello_timer) >= intf->hello_interval) {
                intf->hello_timer = 0;
                auto len = send_produce_hello(intf, data + sizeof(OSPF::Header));
                send_packet(intf, data, len, OSPF::Type::HELLO, ntohl(inet_addr(ALL_SPF_ROUTERS)));
            }

            // For each neighbor
            for (auto& nbr : intf->neighbors) {
                auto nbr_ip = nbr->ip_addr;
                if (nbr->state == Neighbor::State::DOWN) {
                    continue;
                }

                if ((++nbr->rxmt_timer) >= intf->rxmt_interval) {
                    nbr->rxmt_timer = 0;

                    // DD packet
                    if (nbr->state == Neighbor::State::EXSTART || nbr->state == Neighbor::State::EXCHANGE) {
                        auto len = send_produce_dd(intf, data + sizeof(OSPF::Header), nbr);
                        send_packet(intf, data, len, OSPF::Type::DD, nbr_ip);
                        if (!nbr->is_master && nbr->state == Neighbor::State::EXCHANGE) {
                            nbr->event_exchange_done();
                        }
                    }

                    // LSR packet
                    if (nbr->state == Neighbor::State::LOADING || nbr->state == Neighbor::State::EXCHANGE) {
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace OSPF