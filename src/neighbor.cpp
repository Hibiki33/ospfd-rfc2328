#include <cassert>
#include <iostream>

#include "interface.hpp"
#include "lsdb.hpp"
#include "neighbor.hpp"
#include "utils.hpp"

static const char *state_names[]{"DOWN", "ATTEMPT", "INIT", "TWOWAY", "EXSTART", "EXCHANGE", "LOADING", "FULL"};

void Neighbor::event_hello_received() {
    // assert(state == State::DOWN || state == State::ATTEMPT || state == State::INIT);
    if (state >= State::INIT) {
        inactivity_timer = 40;
        return;
    }

    std::cout << "Neighbor " << ip_to_str(ip_addr) << " received hello:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
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
              << "\n\tstate " << state_names[(int)state] << " -> ";
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

void Neighbor::event_2way_received() {
    assert(state == State::INIT || state >= State::TWOWAY);
    if (state >= State::TWOWAY) {
        return;
    }
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " received 2way:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
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
            dd_seq_num = 0;
            is_master = false;
            break;
        }
    }
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_negotiation_done() {
    assert(state == State::EXSTART);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " negotiation done:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    // 初始化dd_summary_list
    this_lsdb.lock();
    for (auto& rlsa : this_lsdb.router_lsas) {
        db_summary_list.push_back(&rlsa->header);
    }
    for (auto& nlsa : this_lsdb.network_lsas) {
        db_summary_list.push_back(&nlsa->header);
    }
    for (auto& slsa : this_lsdb.summary_lsas) {
        db_summary_list.push_back(&slsa->header);
    }
    this_lsdb.unlock();
    state = State::EXCHANGE;
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_exchange_done() {
    assert(state == State::EXCHANGE);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " exchange done:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    // state = State::LOADING;
    link_state_request_list_mtx.lock();
    if (link_state_request_list.empty()) {
        state = State::FULL;
        MAKE_ROUTER_LSA(nullptr);
        if (host_interface->designated_router == host_interface->ip_addr) {
            MAKE_NETWORK_LSA(host_interface);
        }
    } else {
        state = State::LOADING;
    }
    link_state_request_list_mtx.unlock();
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_bad_lsreq() {
    assert(state >= State::EXCHANGE);
    // simarlar to event_seq_number_mismatch
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " bad lsreq:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    state = State::EXSTART;
    dd_seq_num = 0;
    is_master = false;
    link_state_rxmt_list.clear();
    db_summary_list.clear();
    link_state_request_list.clear();
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_loading_done() {
    assert(state == State::LOADING);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " loading done:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    state = State::FULL;
    MAKE_ROUTER_LSA(nullptr);
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_adj_ok() {
    assert(state >= State::TWOWAY);
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " adj ok:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    if (state == State::TWOWAY) {
        if (estab_adj()) {
            state = State::EXSTART;
            dd_seq_num = 0;
            is_master = false;
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
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " seq number mismatch:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    state = State::EXSTART;
    dd_seq_num = 0;
    is_master = false;
    link_state_rxmt_list.clear();
    db_summary_list.clear();
    link_state_request_list.clear();
    // 重新发空的DD包
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_1way_received() {
    assert(state >= State::TWOWAY || state == State::INIT);
    if (state == State::INIT) {
        return;
    }
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " received 1way:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    state = State::INIT;
    inactivity_timer = 40;
    link_state_rxmt_list.clear();
    db_summary_list.clear();
    link_state_request_list.clear();
    std::cout << state_names[(int)state] << std::endl;
}

// Force to kill the neighbor, Any -> State::DOWN
void Neighbor::event_kill_nbr() {
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " kill:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    state = State::DOWN;
    inactivity_timer = 0;
    link_state_rxmt_list.clear();
    db_summary_list.clear();
    link_state_request_list.clear();
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_inactivity_timer() {
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " inactivity timer:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    state = State::DOWN;
    link_state_rxmt_list.clear();
    db_summary_list.clear();
    link_state_request_list.clear();
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_ll_down() {
    std::cout << "Neighbor " << ip_to_str(ip_addr) << " ll down:"
              << "\n\tstate " << state_names[(int)state] << " -> ";
    state = State::DOWN;
    inactivity_timer = 0;
    link_state_rxmt_list.clear();
    db_summary_list.clear();
    link_state_request_list.clear();
    std::cout << state_names[(int)state] << std::endl;
}
