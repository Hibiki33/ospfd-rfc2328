#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include "interface.hpp"
#include "lsdb.hpp"
#include "neighbor.hpp"
#include "transit.hpp"

namespace OSPF {

std::atomic<bool> running(false);
// int recv_fd;

void recv_loop() {
    iphdr *ip_hdr;
    Interface *intf;
    char recv_frame[ETH_FRAME_LEN];
    auto recv_packet = recv_frame + sizeof(ethhdr);
    while (running) {
        for (auto& intf : this_interfaces) {
            memset(recv_frame, 0, ETH_FRAME_LEN);

            // auto recv_size = recv(recv_fd, recv_frame, ETH_FRAME_LEN, 0);
            auto recv_size = recvfrom(intf->recv_fd, recv_frame, ETH_FRAME_LEN, 0, nullptr, nullptr);
            if (recv_size < sizeof(iphdr)) {
                perror("recv_loop: not receive enough data");
            }

            // 解析IP头部
            ip_hdr = reinterpret_cast<iphdr *>(recv_packet);
            auto src_ip = ntohl(ip_hdr->saddr);
            auto dst_ip = ntohl(ip_hdr->daddr);

            // 处理ICMP数据包
            // if (ip_hdr->protocol == IPPROTO_ICMP) {
            //     forward_icmp(recv_packet, recv_size, src_ip, dst_ip);
            //     continue;
            // }

            // 如果不是OSPF协议的数据包
            if (ip_hdr->protocol != IPPROTO_OSPF) {
                continue;
            }

            auto ospf_hdr = reinterpret_cast<OSPF::Header *>(recv_packet + sizeof(iphdr));
            ospf_hdr->network_to_host();

            // 如果是本机发送的数据包
            if (ospf_hdr->router_id == ntohl(inet_addr(THIS_ROUTER_ID))) {
                continue;
            }

            // 查找接口
            // 这里是有问题的，因为组播不一定是这个接口收到的，但是我只有一个接口...
            // 现在不需要了，因为接口对应了:)
            // auto intf_it = std::find_if(this_interfaces.begin(), this_interfaces.end(), [dst_ip](Interface *intf) {
            //     return dst_ip == intf->ip_addr || dst_ip == ntohl(inet_addr(ALL_SPF_ROUTERS));
            // });
            // if (intf_it == this_interfaces.end()) {
            //     continue;
            // }
            // intf = *intf_it;

            switch (ospf_hdr->type) {
            case OSPF::Type::HELLO:
                process_hello(intf, reinterpret_cast<char *>(ospf_hdr), src_ip);
                break;
            case OSPF::Type::DD:
                process_dd(intf, reinterpret_cast<char *>(ospf_hdr), src_ip);
                break;
            case OSPF::Type::LSR:
                process_lsr(intf, reinterpret_cast<char *>(ospf_hdr), src_ip);
                break;
            case OSPF::Type::LSU:
                process_lsu(intf, reinterpret_cast<char *>(ospf_hdr), src_ip);
                break;
            case OSPF::Type::LSACK:
                // process_lsack(intf, reinterpret_cast<char *>(ospf_hdr), src_ip);
                break;
            default:
                break;
            }
        }
    }
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
                auto len = produce_hello(intf, data + sizeof(OSPF::Header));
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
                    // Exstart状态，发送空的DD包
                    if (nbr->state == Neighbor::State::EXSTART) {
                        // 空的dd包只在此处生成
                        nbr->last_dd_data_len = produce_dd(nbr->last_dd_data + sizeof(OSPF::Header), nbr);
                        send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr_ip);
                    }
                    // master + Exchange状态，没收到确认，重传dd包
                    if (!nbr->is_master && nbr->state == Neighbor::State::EXCHANGE) {
                        send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr_ip);
                    }

                    // LSR packet
                    if (nbr->state == Neighbor::State::EXCHANGE || nbr->state == Neighbor::State::LOADING) {
                        auto len = produce_lsr(data + sizeof(OSPF::Header), nbr);
                        if (len != 0) {
                            send_packet(intf, data, len, OSPF::Type::LSR, nbr_ip);
                        }
                    }
                }
            }
        }

        // 睡眠1s，实现timer
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace OSPF