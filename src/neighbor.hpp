#pragma once

#include <cstdint>
#include <list>

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

    /* 最后一个收到的DD包 */
    uint32_t last_dd_seq_num;
    uint32_t last_dd_data_len;
    char last_dd_data[1024];

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
    std::list<LSA::Base *> link_state_list;

    /* Loading状态下需要请求的链路状态数据 */
    std::list<LSA::Base *> link_state_request_list;

public:
    Neighbor(in_addr_t ip_addr, Interface *interface) : ip_addr(ip_addr), host_interface(interface) {
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
};