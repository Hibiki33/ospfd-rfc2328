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

#include "interface.hpp"
#include "neighbor.hpp"
#include "transit.hpp"

namespace OSPF {

std::atomic<bool> running;

// 发送IP包，包含OSPF报文
void send_packet(const char *data, size_t len, OSPF::Type type, in_addr_t dst, Interface *intf) {
    // 创建一个socket
    auto socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_OSPF);
    if (socket_fd < 0) {
        perror("send_packet: socket_fd init");
    }

    // 将socket绑定到指定的接口
    ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, THIS_ROUTER_NAME);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0) {
        perror("send_packet: setsockopt");
    }

    // 构造目标地址
    sockaddr_in dst_sockaddr;
    memset(&dst_sockaddr, 0, sizeof(dst_sockaddr));
    dst_sockaddr.sin_family = AF_INET;
    dst_sockaddr.sin_addr.s_addr = htonl(dst);

    // 构造发送数据包
    char packet[ETH_DATA_LEN];
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

    // 拷贝数据
    memcpy(packet + sizeof(OSPF::Header), data, len);

    // 计算校验和
    ospf_header->checksum = htons(crc_checksum(packet, packet_len));

    // 发送数据包
    if (sendto(socket_fd, packet, packet_len, 0, reinterpret_cast<sockaddr *>(&dst_sockaddr),
               sizeof(dst_sockaddr)) < 0) {
        perror("send_packet: sendto");
    }
}

static void recv_process_hello(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_hello = reinterpret_cast<OSPF::Hello *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor(src_ip);
    if (nbr == nullptr) {
        nbr = intf->add_neighbor(src_ip);
    }
    nbr->id = ospf_hdr->router_id; // hdr已经是host字节序
    auto prev_ndr = nbr->designated_router;
    auto prev_nbdr = nbr->backup_designated_router;
    nbr->designated_router = ntohl(ospf_hello->designated_router);
    nbr->backup_designated_router = ntohl(ospf_hello->backup_designated_router);
    nbr->priority = ntohl(ospf_hello->router_priority);

    nbr->event_hello_received();

    auto to_2way = false;
    // 1way/2way: hello报文中的neighbors列表中包含自己
    in_addr_t *attached_nbr = ospf_hello->neighbors;
    while (attached_nbr != reinterpret_cast<in_addr_t *>(ospf_packet + ospf_hdr->length)) {
        if (*attached_nbr == inet_addr(THIS_ROUTER_ID)) {
            to_2way = true;
            break;
        }
        attached_nbr++;
    }
    if (to_2way) {
        nbr->event_2way_received();
    } else {
        nbr->event_1way();
        return;
    }

    if (nbr->designated_router == nbr->ip_addr && nbr->backup_designated_router == 0x00000000 &&
        intf->state == Interface::State::WAITING) {
        intf->event_backup_seen();
    } else if ((prev_ndr == nbr->ip_addr) ^ (nbr->designated_router == nbr->ip_addr)) {
        intf->event_neighbor_change();
    }
    if (nbr->backup_designated_router == nbr->ip_addr && intf->state == Interface::State::WAITING) {
        intf->event_backup_seen();
    } else if ((prev_nbdr == nbr->ip_addr) ^ (nbr->backup_designated_router == nbr->ip_addr)) {
        intf->event_neighbor_change();
    }
}

static void recv_process_dd(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_dd = reinterpret_cast<OSPF::DD *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor(src_ip);
}

void recv_loop() {
    // 创建一个socket
    auto socket_fd = socket(AF_INET, SOCK_RAW, htons(ETH_P_ALL));
    if (socket_fd < 0) {
        perror("recv_loop: socket_fd init");
    }

    // 将socket绑定到指定的接口
    ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, THIS_ROUTER_NAME);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0) {
        perror("recv_loop: setsockopt");
    }

    iphdr *ip_hdr;
    Interface *intf;
    char recv_frame[ETH_FRAME_LEN];
    auto recv_packet = recv_frame + sizeof(ethhdr);
    while (running) {
        memset(recv_frame, 0, ETH_FRAME_LEN);
        auto recv_size = recv(socket_fd, recv_frame, ETH_FRAME_LEN, 0);
        if (recv_size < 0) {
            perror("recv_loop: recv");
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
        auto intf_it =
            std::find_if(this_interfaces.begin(), this_interfaces.end(), [dst_ip](Interface *intf) {
                return dst_ip == intf->ip_addr || dst_ip == ntohl(inet_addr(OSPF_ALL_SPF_ROUTERS));
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

void send_loop() {
}

} // namespace OSPF