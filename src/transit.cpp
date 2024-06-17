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
        nbr->event_1way_received();
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
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_dd = reinterpret_cast<OSPF::DD *>(ospf_packet + sizeof(OSPF::Header));
    Neighbor *nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);

    ospf_hdr->network_to_host();
    ospf_dd->network_to_host();

recv_dd_nbr_state_switch:
    switch (nbr->state) {
    case Neighbor::State::DOWN:
    case Neighbor::State::ATTEMPT:
    case Neighbor::State::TWOWAY:
        return;
    case Neighbor::State::INIT:
        if (ospf_dd->flags & DD_FLAG_I) {
            nbr->event_2way_received();
        }
        // 需要根据新的状态重新进入switch
        goto recv_dd_nbr_state_switch;
    case Neighbor::State::EXSTART:
        nbr->dd_options = ospf_dd->options;
        // 有点奇怪，但是rfc2328上确实是这么写的
        if (ospf_dd->flags & DD_FLAG_ALL && nbr->id > ntohl(inet_addr(THIS_ROUTER_ID))) {
            nbr->is_master = true;
            nbr->dd_seq_num = ospf_dd->sequence_number;
        } else if (!(ospf_dd->flags & DD_FLAG_MS) && !(ospf_dd->flags & DD_FLAG_I) &&
                   ospf_dd->sequence_number == nbr->dd_seq_num && nbr->id < ntohl(inet_addr(THIS_ROUTER_ID))) {
            nbr->is_master = false;
        }
        nbr->event_negotiation_done();
        break;
    case Neighbor::State::EXCHANGE:
        // 如果收到了重复的DD包
        if (nbr->dd_seq_num == ospf_dd->sequence_number) {
            // master，丢弃重复的DD包
            // slave，要求重传DD包
            if (nbr->is_master) {
                nbr->dd_rtmx = true;
            }
            return;
        } else {
            // 主从关系不匹配
            if (ospf_dd->flags & DD_FLAG_MS && !nbr->is_master) {
                nbr->event_seq_number_mismatch();
                return;
            }
            // 意外设定了I标志
            if (ospf_dd->flags & DD_FLAG_I) {
                nbr->event_seq_number_mismatch();
                return;
            }
        }
        // 如果选项域与过去收到的不一致
        if (nbr->dd_options != ospf_dd->options) {
            nbr->event_seq_number_mismatch();
            return;
        }
        // 对于slave，下一个包应当是邻接记录的dd_seq_num + 1
        if (nbr->is_master && ospf_dd->sequence_number == nbr->dd_seq_num + 1) {
            nbr->dd_seq_num = ospf_dd->sequence_number;
            nbr->last_dd_data_len = ospf_hdr->length;
            // 将lsahdrs拷贝到用于确认的dd_data中
            memcpy(nbr->last_dd_data + sizeof(OSPF::Header) + sizeof(OSPF::DD), ospf_dd->lsahdrs,
                   ospf_hdr->length - sizeof(OSPF::DD));
            nbr->dd_ack = true;
            // 将其lsahdr加入link_state_request_list
            auto num_lsahdrs = (ospf_hdr->length - sizeof(OSPF::DD)) / sizeof(LSA::Header);
            for (auto i = 0; i < num_lsahdrs; i++) {
                auto lsahdr = reinterpret_cast<LSA::Header *>(ospf_dd->lsahdrs + i * sizeof(LSA::Header));
                if (lsahdr->type == LSA::Type::ROUTER) {
                    if (this_lsdb.get_router_lsa(lsahdr->link_state_id, lsahdr->advertising_router) == nullptr) {
                        nbr->link_state_request_list.push_back(
                            {(uint32_t)LSA::Type::ROUTER, lsahdr->link_state_id, lsahdr->advertising_router});
                    }
                } else if (lsahdr->type == LSA::Type::NETWORK) {
                    if (this_lsdb.get_network_lsa(lsahdr->link_state_id, lsahdr->advertising_router) == nullptr) {
                        nbr->link_state_request_list.push_back(
                            {(uint32_t)LSA::Type::NETWORK, lsahdr->link_state_id, lsahdr->advertising_router});
                    }
                }
            }
        // 对于master，下一个包应当为邻居记录的dd_seq_num
        } else if (!nbr->is_master && ospf_dd->sequence_number == nbr->dd_seq_num) {
            nbr->dd_seq_num += 1;
            // 这里就不考虑会有一次发不完的lsahdr的情况了
            // 理论上slave应当在确认的dd中包含上一次发送的lsahdrs的列表
            // 但这里忽略了多次发送的情况，因此直接清空即可
            nbr->db_summary_list.clear();
        } else {
            nbr->event_seq_number_mismatch();
            return;
        }
        // 如果收到清除了M标志的DD包
        if (ospf_dd->flags & DD_FLAG_M) {
            // 从机始终比主机早生成这一事件
            nbr->event_exchange_done();
        }
        break;
    case Neighbor::State::LOADING:
    case Neighbor::State::FULL:
        // TODO:
        break;
    default:
        break;
    }
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
            recv_process_dd(intf, reinterpret_cast<char *>(ospf_hdr), src_ip);
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
    auto dd = reinterpret_cast<OSPF::DD *>(body);
    dd->interface_mtu = ETH_DATA_LEN;
    dd->options = 0x02;
    dd->sequence_number = nbr->dd_seq_num;
    dd->flags = 0;
    if (nbr->is_master) {
        dd->flags |= DD_FLAG_MS;
    }

    if (nbr->state == Neighbor::State::EXSTART) {
        // 发送空的DD包
        dd->flags |= DD_FLAG_I | DD_FLAG_M;
        dd->host_to_network(0);
        return sizeof(OSPF::DD);
    } else if (nbr->state == Neighbor::State::EXCHANGE) {
        if (nbr->dd_ack) {
            // slave需要确认时
            // FLAG_M和FLAG_I都不应该置位
            // 不需要构造lsahdrs，因为已经在recv_process_dd中处理了
            dd->host_to_network(0);
            return nbr->last_dd_data_len;
        } else {
            // master
            // 这里就不考虑会有一次发不完的lsahdr的情况了
            // FLAG_M和FLAG_I都不应该置位
            auto lsahdr = dd->lsahdrs;
            for (auto& lsa : nbr->db_summary_list) {
                memcpy(lsahdr, lsa, sizeof(LSA::Header));
                lsahdr++;
            }
            dd->host_to_network(nbr->db_summary_list.size());
            return sizeof(OSPF::DD) + sizeof(LSA::Header) * nbr->db_summary_list.size();
        }
    } else if (nbr->state >= Neighbor::State::LOADING) {
        // TODO:
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
                auto len = send_produce_hello(intf, data + sizeof(OSPF::Header));
                send_packet(intf, data, len, OSPF::Type::HELLO, ntohl(inet_addr(ALL_SPF_ROUTERS)));
            }

            // For each neighbor
            for (auto& nbr : intf->neighbors) {
                auto nbr_ip = nbr->ip_addr;
                if (nbr->state == Neighbor::State::DOWN) {
                    continue;
                }

                // slave收到重复dd，重发确认
                if (nbr->dd_rtmx) {
                    send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr_ip);
                    nbr->dd_rtmx = false;
                }

                // slave收到dd，发送用于确认的dd
                if (nbr->dd_ack) {
                    auto len = send_produce_dd(intf, nbr->last_dd_data + sizeof(OSPF::Header), nbr);
                    send_packet(intf, nbr->last_dd_data, len, OSPF::Type::DD, nbr_ip);
                    nbr->dd_ack = false;
                }

                if ((++nbr->rxmt_timer) >= intf->rxmt_interval) {
                    nbr->rxmt_timer = 0;

                    // DD packet
                    if ( // Exstart状态，发送空的DD包
                        (nbr->state == Neighbor::State::EXSTART) ||
                        // Exchange状态，这里是对于master的处理，没收到确认，重传dd包
                        (!nbr->is_master && nbr->state == Neighbor::State::EXCHANGE)) {
                        auto len = send_produce_dd(intf, data + sizeof(OSPF::Header), nbr);
                        send_packet(intf, data, len, OSPF::Type::DD, nbr_ip);
                    }

                    // LSR packet
                    if (nbr->state == Neighbor::State::LOADING || nbr->state == Neighbor::State::EXCHANGE) {
                    }
                }
            }
        }

        // 睡眠1s，实现timer
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace OSPF