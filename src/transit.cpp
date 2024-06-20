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

constexpr size_t dd_max_lsahdr_num = (ETH_DATA_LEN - sizeof(OSPF::Header) - sizeof(OSPF::DD)) / sizeof(LSA::Header);

// forward declaration
static void send_packet(Interface *intf, char *packet, size_t len, OSPF::Type type, in_addr_t dst);
static size_t produce_hello(Interface *intf, char *body);
static void process_hello(Interface *intf, char *ospf_packet, in_addr_t src_ip);
static size_t produce_dd(Interface *intf, char *body, Neighbor *nbr);
static void process_dd(Interface *intf, char *ospf_packet, in_addr_t src_ip);
static size_t produce_lsr(Interface *intf, char *body, Neighbor *nbr);
static void process_lsr(Interface *intf, char *ospf_packet, in_addr_t src_ip);
static size_t produce_lsu(Interface *intf, char *body, Neighbor *nbr, std::list<LSA::Base *>& lsa_update_list);
static void process_lsu(Interface *intf, char *ospf_packet, in_addr_t src_ip);
static size_t produce_lsack(Interface *intf, char *body, Neighbor *nbr, std::list<LSA::Base *>& lsa_ack_list);
static void process_lsack(Interface *intf, char *ospf_packet, in_addr_t src_ip);

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

static size_t produce_hello(Interface *intf, char *body) {
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

static void process_hello(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
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

static size_t produce_dd(Interface *intf, char *body, Neighbor *nbr) {
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
        // master: 传入通用的buffer
        // slave: 传入last_dd_data
        // FLAG_I不应该置位
        // db_summary_list可能为空
        auto lsahdr = dd->lsahdrs;
        size_t lsahdr_cnt = 0;
        nbr->db_summary_list_mtx.lock();
        for (auto& lsa : nbr->db_summary_list) {
            memcpy(lsahdr, lsa, sizeof(LSA::Header));
            lsahdr++;
            lsahdr_cnt++;
            if (lsahdr_cnt >= dd_max_lsahdr_num) {
                break;
            }
        }
        nbr->db_summary_list_mtx.unlock();
        dd->host_to_network(lsahdr_cnt);
        if (lsahdr_cnt >= dd_max_lsahdr_num) {
            // 如果还有lsahdr没有发送
            dd->flags |= DD_FLAG_M;
        } else if (nbr->is_master && nbr->dd_recv_no_more) {
            // 如果接收到清除了M位的包，而且从机发送的包也将M位清0
            nbr->event_exchange_done();
            nbr->db_summary_list.clear(); // slave最后一个dd包发送后，在此处清空
        }
        nbr->dd_lsahdr_cnt = lsahdr_cnt;
        return sizeof(OSPF::DD) + sizeof(LSA::Header) * lsahdr_cnt;
    } else if (nbr->state >= Neighbor::State::LOADING) {
        // TODO:
    }
}

static void process_dd(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_dd = reinterpret_cast<OSPF::DD *>(ospf_packet + sizeof(OSPF::Header));
    Neighbor *nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);

    ospf_hdr->network_to_host();
    ospf_dd->network_to_host();

    auto save_to_lsr_list = [&]() {
        auto num_lsahdrs = (ospf_hdr->length - sizeof(OSPF::DD)) / sizeof(LSA::Header);
        for (auto i = 0; i < num_lsahdrs; ++i) {
            auto lsahdr = reinterpret_cast<LSA::Header *>(ospf_dd->lsahdrs + i * sizeof(LSA::Header));
            lsahdr->network_to_host();
            nbr->link_state_request_list_mtx.lock();
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
            nbr->link_state_request_list_mtx.unlock();
        }
    };

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
        if ( // master，丢弃重复的DD包
            (!nbr->is_master && ospf_dd->sequence_number < nbr->dd_seq_num) ||
            // slave，要求重传DD包
            (nbr->is_master && nbr->dd_seq_num == ospf_dd->sequence_number)) {
            if (nbr->is_master) {
                assert(nbr->last_dd_data_len > 0);
                send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr->ip_addr);
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
        if (nbr->is_master &&
            ospf_dd->sequence_number == nbr->dd_seq_num + 1) { // 对于slave，下一个包应当是邻接记录的dd_seq_num + 1
            // slave
            nbr->dd_seq_num = ospf_dd->sequence_number;
            // 将其lsahdr加入link_state_request_list
            save_to_lsr_list();
            nbr->db_summary_list_mtx.lock();
            while (nbr->dd_lsahdr_cnt > 0) {
                nbr->db_summary_list.pop_front();
                nbr->dd_lsahdr_cnt--;
            }
            nbr->db_summary_list_mtx.unlock();
            if (ospf_dd->flags & DD_FLAG_M) {
                // 从机始终比主机早生成这一事件
                nbr->dd_recv_no_more = true;
                // slave的exchange_done在发送时触发
            }
            auto len = produce_dd(intf, nbr->last_dd_data + sizeof(OSPF::Header), nbr);
            send_packet(intf, nbr->last_dd_data, len, OSPF::Type::DD, nbr->ip_addr);
        } else if (!nbr->is_master &&
                   ospf_dd->sequence_number == nbr->dd_seq_num) { // 对于master，下一个包应当为邻居记录的dd_seq_num
            // master
            nbr->dd_seq_num += 1;
            // 将其lsahdr加入link_state_request_list
            save_to_lsr_list();
            nbr->db_summary_list_mtx.lock();
            while (nbr->dd_lsahdr_cnt > 0) {
                nbr->db_summary_list.pop_front();
                nbr->dd_lsahdr_cnt--;
            }
            // master已经发送了全部的DD包，并收到了清除了M位的包
            if (nbr->db_summary_list.empty() && ospf_dd->flags & DD_FLAG_M) {
                // 从机始终比主机早生成这一事件
                // nbr->dd_recv_no_more = true;
                nbr->event_exchange_done();
            }
            nbr->db_summary_list_mtx.unlock();
        } else {
            nbr->event_seq_number_mismatch();
            return;
        }
        break;
    case Neighbor::State::LOADING:
    case Neighbor::State::FULL:
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
        // slave收到重复的DD包
        if (nbr->is_master) {
            if (ospf_dd->sequence_number == nbr->dd_seq_num) {
                send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr->ip_addr);
            } else {
                nbr->event_seq_number_mismatch();
            }
        }
        break;
    default:
        break;
    }
}

static size_t produce_lsr(Interface *intf, char *body, Neighbor *nbr) {
    // link_state_request_list已经锁住了
    // 其实LSR中应该包含多个LSA的，但是这里实现中LSR只包含一个LSA
    auto lsr = reinterpret_cast<OSPF::LSR *>(body);
    auto ls_req = &nbr->link_state_request_list.front();
    auto req = lsr->reqs;
    req->ls_type = ls_req->ls_type;
    req->link_state_id = ls_req->link_state_id;
    req->advertising_router = ls_req->advertising_router;
    lsr->host_to_network(1);
    return sizeof(req);
}

static void process_lsr(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_lsr = reinterpret_cast<OSPF::LSR *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);

    // 如果邻居不是Exchange、Loading或Full状态，直接丢弃
    if (nbr->state < Neighbor::State::EXCHANGE) {
        return;
    }

    ospf_hdr->network_to_host();
    // ospf_lsr->network_to_host();
    // req的字节序转换放到后面

    auto req = ospf_lsr->reqs;
    auto req_end = reinterpret_cast<decltype(req)>(ospf_packet + ospf_hdr->length);
    char lsu_data[ETH_DATA_LEN];
    std::list<LSA::Base *> lsa_update_list;
    while (req != req_end) {
        req->network_to_host();
        auto lsa = this_lsdb.get_router_lsa(req->link_state_id, req->advertising_router);
        if (lsa == nullptr) {
            nbr->event_bad_lsreq();
        }
        lsa_update_list.push_back(lsa);
        req++;
    }
    auto len = produce_lsu(intf, lsu_data + sizeof(OSPF::Header), nbr, lsa_update_list);
    send_packet(intf, lsu_data, len, OSPF::Type::LSU, src_ip);
}

static size_t produce_lsu(Interface *intf, char *body, Neighbor *nbr, std::list<LSA::Base *>& lsa_update_list) {
    auto lsu = reinterpret_cast<OSPF::LSU *>(body);
    size_t offset = sizeof(OSPF::LSU);
    for (auto& lsa : nbr->lsa_update_list) {
        lsa->to_packet(body + offset);
        lsu->num_lsas += 1;
    }
    lsu->host_to_network();
    return offset;
}

static void process_lsu(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_lsu = reinterpret_cast<OSPF::LSU *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);

    ospf_hdr->network_to_host();
    ospf_lsu->network_to_host();

    std::list<LSA::Base *> lsa_ack_list;
    size_t offset = sizeof(OSPF::Header) + sizeof(OSPF::LSU);
    for (auto i = 0; i < ospf_lsu->num_lsas; ++i) {
        auto lsa = this_lsdb.add_lsa(ospf_packet + offset);
        lsa_ack_list.push_back(lsa);
        offset += lsa->size();
    }

    // 将收到的lsa从link_state_request_list中删除
    nbr->link_state_request_list_mtx.lock();
    for (auto& lsa : lsa_ack_list) {
        auto it = std::find_if(
            nbr->link_state_request_list.begin(), nbr->link_state_request_list.end(), [lsa](OSPF::LSR::Request& req) {
                return req.ls_type == (uint32_t)lsa->header.type && req.link_state_id == lsa->header.link_state_id &&
                       req.advertising_router == lsa->header.advertising_router;
            });
        if (it != nbr->link_state_request_list.end()) {
            nbr->link_state_request_list.erase(it);
        }
    }

    // 准备发送LSAck
    auto len = produce_lsack(intf, ospf_packet, nbr, lsa_ack_list);
    send_packet(intf, ospf_packet, len, OSPF::Type::LSACK, src_ip);
}

static size_t produce_lsack(Interface *intf, char *body, Neighbor *nbr, std::list<LSA::Base *>& lsa_ack_list) {
    auto lsack = reinterpret_cast<OSPF::LSAck *>(body);
    size_t offset = 0;
    for (auto& lsa : lsa_ack_list) {
        memcpy(body + offset, &lsa->header, sizeof(LSA::Header));
        reinterpret_cast<LSA::Header *>(body + offset)->host_to_network();
        offset += sizeof(LSA::Header);
    }
    return offset;
}

static void process_lsack(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_lsack = reinterpret_cast<OSPF::LSAck *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);
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
            break;
        default:
            break;
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
                    if ( // Exstart状态，发送空的DD包
                        (nbr->state == Neighbor::State::EXSTART) ||
                        // Exchange状态，这里是对于master的处理，没收到确认，重传dd包
                        (!nbr->is_master && nbr->state == Neighbor::State::EXCHANGE)) {
                        auto len = produce_dd(intf, data + sizeof(OSPF::Header), nbr);
                        send_packet(intf, data, len, OSPF::Type::DD, nbr_ip);
                    }

                    // LSR packet
                    if (nbr->state == Neighbor::State::EXCHANGE || nbr->state == Neighbor::State::LOADING) {
                        nbr->link_state_request_list_mtx.lock();
                        if (nbr->link_state_request_list.empty() && nbr->state == Neighbor::State::LOADING) {
                            nbr->link_state_request_list_mtx.unlock();
                            nbr->event_loading_done();
                        } else {
                            auto len = produce_lsr(intf, data + sizeof(OSPF::Header), nbr);
                            send_packet(intf, data, len, OSPF::Type::LSR, nbr_ip);
                        }
                        nbr->link_state_request_list_mtx.unlock();
                    }
                }
            }
        }

        // 睡眠1s，实现timer
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace OSPF