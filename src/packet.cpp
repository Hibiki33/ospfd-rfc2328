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

// constexpr size_t dd_max_lsahdr_num = (ETH_DATA_LEN - sizeof(OSPF::Header) - sizeof(OSPF::DD)) / sizeof(LSA::Header);
constexpr size_t dd_max_lsahdr_num = 10ul;

// 发送IP包，包含OSPF报文
void send_packet(Interface *intf, char *packet, size_t len, OSPF::Type type, in_addr_t dst) {
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

size_t produce_hello(Interface *intf, char *body) {
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

void process_hello(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
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

size_t produce_dd(char *body, Neighbor *nbr) {
    auto dd = reinterpret_cast<OSPF::DD *>(body);
    size_t dd_len;
    dd->interface_mtu = ETH_DATA_LEN;
    dd->options = 0x02;
    dd->sequence_number = nbr->dd_seq_num;
    dd->flags = 0;
    if (!nbr->is_master) {
        dd->flags |= DD_FLAG_MS;
    }
    if (nbr->dd_init) {
        dd->flags |= DD_FLAG_I;
    }

    dd_len = sizeof(OSPF::DD);
    if (nbr->dd_init) {
        dd->host_to_network(0);
    } else {
        auto lsahdr = dd->lsahdrs;
        if (nbr->db_summary_list.size() > dd_max_lsahdr_num) {
            std::advance(nbr->db_summary_send_iter, dd_max_lsahdr_num);
            dd->flags |= DD_FLAG_M;
            for (auto it = nbr->db_summary_list.begin(); it != nbr->db_summary_send_iter; ++it) {
                memcpy(lsahdr, *it, sizeof(LSA::Header));
                lsahdr++;
            }
            dd_len += sizeof(LSA::Header) * dd_max_lsahdr_num;
            dd->host_to_network(dd_max_lsahdr_num);
        } else {
            // 本次发送剩下所有lsahdr
            nbr->db_summary_send_iter = nbr->db_summary_list.end();
            for (auto it = nbr->db_summary_list.begin(); it != nbr->db_summary_list.end(); ++it) {
                memcpy(lsahdr, *it, sizeof(LSA::Header));
                lsahdr++;
            }
            dd_len += sizeof(LSA::Header) * nbr->db_summary_list.size();
            dd->host_to_network(nbr->db_summary_list.size());
            // 如果slave已收到!M的包，而且无lsahdr需要发送
            if (nbr->is_master && nbr->dd_recv_no_more) {
                nbr->event_exchange_done();
                nbr->db_summary_list.clear();
            }
        }
    }

    return dd_len;
}

void process_dd(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_dd = reinterpret_cast<OSPF::DD *>(ospf_packet + sizeof(OSPF::Header));
    Neighbor *nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);
    ospf_dd->network_to_host();

    bool dup = nbr->recv_dd_seq_num == ospf_dd->sequence_number;
    nbr->recv_dd_seq_num = ospf_dd->sequence_number;

    bool accept = false;

    switch (nbr->state) {
    case Neighbor::State::DOWN:
    case Neighbor::State::ATTEMPT:
    case Neighbor::State::TWOWAY:
        return;
    case Neighbor::State::INIT:
        nbr->event_2way_received();
        if (nbr->state == Neighbor::State::TWOWAY) {
            return;
        }
        // 如果变为exstart状态，直接进入下一个case
        // 在此处不需要break
    case Neighbor::State::EXSTART:
        nbr->dd_options = ospf_dd->options;
        if (ospf_dd->flags & DD_FLAG_ALL && nbr->id > ntohl(inet_addr(THIS_ROUTER_ID))) {
            nbr->is_master = true;
            nbr->dd_seq_num = ospf_dd->sequence_number;
        } else if (!(ospf_dd->flags & DD_FLAG_MS) && !(ospf_dd->flags & DD_FLAG_I) &&
                   ospf_dd->sequence_number == nbr->dd_seq_num && nbr->id < ntohl(inet_addr(THIS_ROUTER_ID))) {
            nbr->is_master = false;
        } else {
            // 将要成为master收到了第一个DD包，无需处理
            return;
        }
        nbr->dd_init = false;
        nbr->event_negotiation_done();
        if (nbr->is_master) {
            // 如果自己是slave，发送这样一个DD包：
            // 1. MS和I位置0
            // 2. 序列号为邻居的dd_seq_num
            // 3. 包含lsahdr
            // 此时已经是exchange状态，这很重要
            nbr->last_dd_data_len = produce_dd(nbr->last_dd_data + sizeof(OSPF::Header), nbr);
            send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr->ip_addr);
            return;
        }
        // 如果是master，这里收到dd包必然不为空
        // 在切换到exchange状态后按照exchange状态的处理方式处理
        // 这里不需要break
    case Neighbor::State::EXCHANGE:
        // 如果收到了重复的DD包
        if (dup) {
            if (nbr->is_master) {
                // slave需要重传上一个包，master的重传通过计时器实现
                send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr->ip_addr);
            }
            return;
        } else {
            // 主从关系不匹配
            if ((bool)(ospf_dd->flags & DD_FLAG_MS) != nbr->is_master) {
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
        if (nbr->is_master &&                                  // 自己为 slave
            ospf_dd->sequence_number == nbr->dd_seq_num + 1) { // 对于slave，下一个包应当是邻接记录的dd_seq_num + 1
            nbr->dd_seq_num = ospf_dd->sequence_number;
            accept = true;
        } else if (!nbr->is_master &&                             // 自己为master
                   ospf_dd->sequence_number == nbr->dd_seq_num) { // 对于master，下一个包应当为邻居记录的dd_seq_num
            nbr->dd_seq_num += 1;
            accept = true;
        } else {
            nbr->event_seq_number_mismatch();
            return;
        }
        break;
    case Neighbor::State::LOADING:
    case Neighbor::State::FULL:
        // 主从关系不匹配
        if ((bool)(ospf_dd->flags & DD_FLAG_MS) != nbr->is_master) {
            nbr->event_seq_number_mismatch();
            return;
        }
        // 意外设定了I标志
        if (ospf_dd->flags & DD_FLAG_I) {
            nbr->event_seq_number_mismatch();
            return;
        }
        // slave收到重复的DD包
        if (nbr->is_master && dup) {
            send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr->ip_addr);
            return;
        }
        break;
    default:
        break;
    }

    if (accept) {
        // 视为回复，将db_summary_list中上一次发送的lsahdr删除（可能为空）
        nbr->db_summary_list.erase(nbr->db_summary_list.begin(), nbr->db_summary_send_iter);

        // 收到了!M的DD包
        nbr->dd_recv_no_more = !(ospf_dd->flags & DD_FLAG_M);

        // 将收到的lsahdr加入link_state_request_list
        auto num_lsahdrs = (ospf_hdr->length - sizeof(OSPF::Header) - sizeof(OSPF::DD)) / sizeof(LSA::Header);
        LSA::Header *lsahdr = ospf_dd->lsahdrs;
        for (auto i = 0; i < num_lsahdrs; ++i) {
            lsahdr->network_to_host();
            nbr->link_state_request_list_mtx.lock();
            this_lsdb.lock();
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
            } else {
                // TODO: 其他类型的LSA
            }
            this_lsdb.unlock();
            nbr->link_state_request_list_mtx.unlock();
            lsahdr++;
        }

        // 从本质上说，master和slave都需要立即回复
        // master需要在此处完成exchange_done事件
        if (!nbr->is_master) {
            if (nbr->db_summary_list.empty() && nbr->dd_recv_no_more) {
                nbr->event_exchange_done();
                return;
            }
        }
        nbr->last_dd_data_len = produce_dd(nbr->last_dd_data + sizeof(OSPF::Header), nbr);
        send_packet(intf, nbr->last_dd_data, nbr->last_dd_data_len, OSPF::Type::DD, nbr->ip_addr);
    }
}

size_t produce_lsr(char *body, Neighbor *nbr) {
    // 一次性发完得了...
    auto lsr = reinterpret_cast<OSPF::LSR *>(body);
    auto lsr_req = lsr->reqs;
    nbr->link_state_request_list_mtx.lock();
    if (nbr->link_state_request_list.empty()) {
        nbr->link_state_request_list_mtx.unlock();
        // 如果时loading状态且已经没有请求，触发loading_done事件
        if (nbr->state == Neighbor::State::LOADING) {
            nbr->event_loading_done();
        }
        return 0;
    }
    for (auto& req : nbr->link_state_request_list) {
        memcpy(lsr_req, &req, sizeof(OSPF::LSR::Request));
        lsr_req++;
    }
    lsr->host_to_network(nbr->link_state_request_list.size());
    auto len = sizeof(OSPF::LSR::Request) * nbr->link_state_request_list.size();
    nbr->link_state_request_list_mtx.unlock();
    return len;
}

void process_lsr(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_lsr = reinterpret_cast<OSPF::LSR *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);

    // 如果邻居不是Exchange、Loading或Full状态，直接丢弃
    if (nbr->state < Neighbor::State::EXCHANGE) {
        return;
    }
    // req的字节序转换放到后面

    auto req = ospf_lsr->reqs;
    auto req_end = reinterpret_cast<decltype(req)>(ospf_packet + ospf_hdr->length);
    char lsu_data[ETH_DATA_LEN];
    std::list<LSA::Base *> lsa_update_list;
    this_lsdb.lock();
    while (req != req_end) {
        req->network_to_host();
        auto lsa = this_lsdb.get_router_lsa(req->link_state_id, req->advertising_router);
        if (lsa == nullptr) {
            nbr->event_bad_lsreq();
        }
        lsa_update_list.push_back(lsa);
        req++;
    }
    this_lsdb.unlock();

    // 立即回复LSU（其实可以给发send线程一个信号来处理，可以避免一次数组的分配）
    auto len = produce_lsu(lsu_data + sizeof(OSPF::Header), lsa_update_list);
    send_packet(intf, lsu_data, len, OSPF::Type::LSU, src_ip);
}

size_t produce_lsu(char *body, const std::list<LSA::Base *>& lsa_update_list) {
    auto lsu = reinterpret_cast<OSPF::LSU *>(body);
    size_t offset = sizeof(OSPF::LSU);
    for (auto& lsa : lsa_update_list) {
        lsa->to_packet(body + offset); // 此处已经转化位网络字节序
        lsu->num_lsas += 1;
    }
    lsu->host_to_network();
    return offset;
}

void process_lsu(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_lsu = reinterpret_cast<OSPF::LSU *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);
    ospf_lsu->network_to_host();

    // 根据LSU更新数据库，并将其从link_state_request_list中删除
    size_t offset = sizeof(OSPF::Header) + sizeof(OSPF::LSU);
    for (auto i = 0; i < ospf_lsu->num_lsas; ++i) {
        auto lsahdr = reinterpret_cast<LSA::Header *>(ospf_packet + offset);
        LSA::Base *lsa = nullptr;
        this_lsdb.lock();
        if (lsahdr->type == LSA::Type::ROUTER) {
            lsa = new LSA::Router(ospf_packet + offset);
            this_lsdb.add(lsa);
        } else if (lsahdr->type == LSA::Type::NETWORK) {
            lsa = new LSA::Network(ospf_packet + offset);
            this_lsdb.add(lsa);
        } else {
            assert(false && "Not implemented yet");
        }
        this_lsdb.unlock();
        offset += lsa->size();
        // 将收到的lsa从link_state_request_list中删除
        nbr->link_state_request_list_mtx.lock();
        // 理想状态是每次删掉第一个
        auto it = std::find_if(
            nbr->link_state_request_list.begin(), nbr->link_state_request_list.end(), [lsa](OSPF::LSR::Request& req) {
                return req.ls_type == (uint32_t)lsa->header.type && req.link_state_id == lsa->header.link_state_id &&
                       req.advertising_router == lsa->header.advertising_router;
            });
        if (it != nbr->link_state_request_list.end()) {
            nbr->link_state_request_list.erase(it);
        }
        nbr->link_state_request_list_mtx.unlock();
    }

    // loading_done事件在发送LSR时触发

    // 准备发送LSAck
    // auto len = produce_lsack(intf, ospf_packet, nbr, lsa_ack_list);
    // send_packet(intf, ospf_packet, len, OSPF::Type::LSACK, src_ip);
    // 貌似在同步数据库的时候不需要发
    // TODO: check一下
}

size_t produce_lsack(Interface *intf, char *body, Neighbor *nbr, const std::list<LSA::Base *>& lsa_ack_list) {
    auto lsack = reinterpret_cast<OSPF::LSAck *>(body);
    size_t offset = 0;
    for (auto& lsa : lsa_ack_list) {
        memcpy(body + offset, &lsa->header, sizeof(LSA::Header));
        reinterpret_cast<LSA::Header *>(body + offset)->host_to_network();
        offset += sizeof(LSA::Header);
    }
    return offset;
}

void process_lsack(Interface *intf, char *ospf_packet, in_addr_t src_ip) {
    auto ospf_hdr = reinterpret_cast<OSPF::Header *>(ospf_packet);
    auto ospf_lsack = reinterpret_cast<OSPF::LSAck *>(ospf_packet + sizeof(OSPF::Header));
    auto nbr = intf->get_neighbor_by_ip(src_ip);
    assert(nbr != nullptr);
}

void flood_lsa(LSA::Base *lsa) {
    char buf[ETH_DATA_LEN];
    auto len = produce_lsu(buf + sizeof(OSPF::Header), {lsa});
    for (auto& intf : this_interfaces) {
        if (intf->state == Interface::State::DROTHER || intf->state == Interface::State::BACKUP ||
            intf->state == Interface::State::POINT2POINT) {
            send_packet(intf, buf, len, OSPF::Type::LSU, inet_addr(ALL_SPF_ROUTERS));
        }
    }
}

} // namespace OSPF