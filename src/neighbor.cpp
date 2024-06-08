#include <iostream>
#include <cassert>

#include "neighbor.hpp"
#include "interface.hpp"
#include "utils.hpp"    

static const char *state_names[] {
    "DOWN", "ATTEMPT", "INIT", "TWOWAY", "EXSTART", "EXCHANGE", "LOADING", "FULL"
};

void Neighbor::event_hello_received() {
    assert(state == State::DOWN || state == State::ATTEMPT || state == State::INIT);
    std::cout << "Neighbor " << ip_to_string(ip_addr) << " received hello:"
              << "\tstate " << state_names[(int)state] << " -> ";
    switch (state) {
    case State::DOWN:
    case State::ATTEMPT:
    case State::INIT:
        state = State::INIT;
        inactivity_timer = 40;
        break;
    default:
        break;
    }
    std::cout << state_names[(int)state] << std::endl;
}

// NBMA only, need to use with sending hello packet
void Neighbor::event_start() {
    assert(state == State::DOWN);
    std::cout << "Neighbor " << ip_to_string(ip_addr) << " start:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::ATTEMPT;
    std::cout << state_names[(int)state] << std::endl;
}

// TODO: need to implement details
void Neighbor::event_2way_received() {
    assert(state == State::INIT);
    std::cout << "Neighbor " << ip_to_string(ip_addr) << " received 2way:"
              << "\tstate " << state_names[(int)state] << " -> ";
    switch (host_interface->type) {
    case Interface::Type::BROADCAST:
    case Interface::Type::NBMA:
        state = State::TWOWAY;
        break;
    default:
        state = State::EXSTART;
        dd_seq_num = 0;
        is_master = true;
        break;
    }
}

void Neighbor::event_negotiation_done() {
    // TODO: not implemented
}

void Neighbor::event_exchange_done() {
    // TODO: not implemented
}

void Neighbor::event_bad_lsreq() {
    // TODO: not implemented
}

void Neighbor::event_loading_done() {
    std::cout << "Neighbor " << ip_to_string(ip_addr) << " loading done:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::FULL;
    std::cout << state_names[(int)state] << std::endl;
}

void Neighbor::event_adj_ok() {
    // TODO: not implemented
}

void Neighbor::event_seq_number_mismatch() {
    // TODO: not implemented
}

void Neighbor::event_1way_received() {
    // TODO: not implemented
}

void Neighbor::event_kill_nbr() {
    // TODO: not implemented
}

void Neighbor::event_inactivity_timer() {
    // TODO: not implemented
}

void Neighbor::event_ll_down() {
    // TODO: not implemented
}
