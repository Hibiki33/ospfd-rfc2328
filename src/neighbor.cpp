#include <cassert>
#include <iostream>

#include "interface.hpp"
#include "neighbor.hpp"
#include "utils.hpp"

static const char *state_names[]{"DOWN", "ATTEMPT", "INIT", "TWOWAY", "EXSTART", "EXCHANGE", "LOADING", "FULL"};

void Neighbor::event_hello_received() {
    // assert(state == State::DOWN || state == State::ATTEMPT || state == State::INIT);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " received hello:"
              << "\tstate " << state_names[(int)state] << " -> ";
    switch (state) {
    case State::DOWN:
    case State::ATTEMPT:
    case State::INIT:
        state = State::INIT;
        inactivity_timer = 40;
        break;
    default:
        // 如果在Init之上的状态收到Hello，无须操作
        break;
    }
    std::cout << state_names[(int)state] << std::endl;
}

// NBMA only, need to use with sending hello packet
void Neighbor::event_start() {
    assert(state == State::DOWN);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " start:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::ATTEMPT;
    std::cout << state_names[(int)state] << std::endl;
}

/* 是否需要建立邻接 */
bool Neighbor::estab_adj() noexcept {
    // from rfc2328 10.4
    return
        // 网络类型为点对点
        host_interface->type == Interface::Type::P2P ||
        // 网络类型为点到多点
        host_interface->type == Interface::Type::P2MP ||
        // 网络类型为虚拟通道
        host_interface->type == Interface::Type::VIRTUAL ||
        // 路由器自身是DR
        host_interface->ip_addr == designated_router ||
        // 路由器自身是BDR
        host_interface->ip_addr == backup_designated_router ||
        // 邻居是DR
        ip_addr == designated_router ||
        // 邻居是BDR
        ip_addr == backup_designated_router;
}

// TODO: need to implement details
void Neighbor::event_2way_received() {
    assert(state == State::INIT || state >= State::TWOWAY);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " received 2way:"
              << "\tstate " << state_names[(int)state] << " -> ";
    if (state == State::INIT) {
        switch (host_interface->type) {
        case Interface::Type::BROADCAST:
        case Interface::Type::NBMA:
            // 判断是否需要建立邻接
            if (!(estab_adj())) {
                // 不需要建立邻接
                state = State::TWOWAY;
                break;
            }
        default:
            // 需要建立邻接 / P2P / P2MP / VIRTUAL
            state = State::EXSTART;
            dd_seq_num = 0x80000000 + 1;
            is_master = true;
            // TODO: prepare empty DD packet
            break;
        }
    }
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_negotiation_done() {
    assert(state == State::EXSTART);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " negotiation done:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::EXCHANGE;
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_exchange_done() {
    assert(state == State::EXCHANGE);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " exchange done:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::LOADING;
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_bad_lsreq() {
    assert(state >= State::EXCHANGE);
    // TODO: not implemented
}

void Neighbor::event_loading_done() {
    assert(state == State::LOADING);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " loading done:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::FULL;
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_adj_ok() {
    assert(state >= State::TWOWAY);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " adj ok:"
              << "\tstate " << state_names[(int)state] << " -> ";
    if (state == State::TWOWAY) {
        if (estab_adj()) {
            state = State::EXSTART;
            dd_seq_num = 0x80000000 + 1;
            is_master = true;
        }
    } else if (state >= State::EXSTART) {
        if (!estab_adj()) {
            state = State::TWOWAY;
        }
    }
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_seq_number_mismatch() {
    assert(state >= State::EXCHANGE);
    // TODO: not implemented
}

// TODO: 下面4个event需要实现：清除连接状态重传列表、数据库汇总列表和连接状态请求列表中的LSA

void Neighbor::event_1way_received() {
    assert(state >= State::TWOWAY || state == State::INIT);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " received 1way:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::INIT;
    inactivity_timer = 40;
    std::cout << state_names[(int)state] << std::endl;
}

// Force to kill the neighbor, Any -> State::DOWN
void Neighbor::event_kill_nbr() {
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " kill:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::DOWN;
    inactivity_timer = 0;
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_inactivity_timer() {
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " inactivity timer:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::DOWN;
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_ll_down() {
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " ll down:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::DOWN;
    inactivity_timer = 0;
    std::cout << state_names[(int)state] << std::endl;
}
