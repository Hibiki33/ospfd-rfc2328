#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>

#include <netinet/if_ether.h>
#include <netinet/in.h>

#include "packet.hpp"

class Interface;

/*
 * 表示OSPF路由器的邻居数据结构：
 * - 每个会话被限定在特定的路由器接口上，并由邻居的OSPF路由器标识；
 * - 每个独立的会话被称为“邻居”
 */
class Neighbor {
public:
    /* 邻居的状态 */
    enum class State {
        DOWN = 0,
        ATTEMPT,
        INIT,
        TWOWAY,
        EXSTART,
        EXCHANGE,
        LOADING,
        FULL
    } state = State::DOWN;

    /* 非活跃计时器 */
    uint32_t inactivity_timer = 40;

    /* 是否为master */
    bool is_master = false;

    /* DD包的序列号 */
    uint32_t dd_seq_num;

    /* 最后一个DD包，用于重传 */
    // uint32_t last_dd_seq_num;
    uint32_t last_dd_data_len;
    char last_dd_data[ETH_DATA_LEN];
    /* 记录上一次传输的dd包中lsahdr的数量 */
    size_t dd_lsahdr_cnt = 0;
    /* 是否收到了!FLAG_M的DD包 */
    bool dd_recv_no_more = false;

    /* 邻居的路由器标识 */
    uint32_t id;
    /* 邻居的优先级 */
    uint32_t priority;
    /* 邻居的IP地址 */
    in_addr_t ip_addr;

    /* 邻居的指定路由器 */
    in_addr_t designated_router;
    /* 邻居的备份指定路由器 */
    in_addr_t backup_designated_router;

    Interface *host_interface;

    /* 邻居的重传计时器 */
    uint32_t rxmt_timer = 0;

    /* 需要重传的链路状态数据 */
    std::list<LSA::Base *> link_state_rxmt_list;

    /* Exchange状态下的链路状态数据 */
    std::list<LSA::Header *> db_summary_list;
    std::mutex db_summary_list_mtx;

    /* Exchange和Loading状态下需要请求的链路状态数据 */
    std::list<OSPF::LSR::Request> link_state_request_list;
    std::mutex link_state_request_list_mtx;

    /* Exchange和Loading状态下收到的链路状态请求，准备用于lsu中发送 */
    std::list<LSA::Base *> lsa_update_list;

    /* 邻居DD选项 */
    uint8_t dd_options;

public:
    Neighbor(in_addr_t ip_addr, Interface *interface) : ip_addr(ip_addr), host_interface(interface) {
        // dd_rtmx = false;
        // dd_ack = false;
    }
    ~Neighbor() = default;

public:
    void event_hello_received();
    void event_start();
    void event_2way_received();
    void event_negotiation_done();
    void event_exchange_done();
    void event_bad_lsreq();
    void event_loading_done();
    void event_adj_ok();
    void event_seq_number_mismatch();
    void event_1way_received();
    void event_kill_nbr();
    void event_inactivity_timer();
    void event_ll_down();

private:
    bool estab_adj() noexcept;
};