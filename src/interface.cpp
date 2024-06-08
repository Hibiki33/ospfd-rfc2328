#include <algorithm>
#include <cassert>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "interface.hpp"
#include "neighbor.hpp"
#include "utils.hpp"

std::vector<Interface *> this_interfaces;

static const char *state_names[] = {"DOWN",    "LOOPBACK", "WAITING", "POINT2POINT",
                                    "DROTHER", "BACKUP",   "DR"};

void Interface::elect_designated_router() {
    // printf("\n\tStart electing DR and BDR...\n");
    std::cout << "Start electing DR and BDR..." << std::endl;

    std::list<Neighbor *> candidates;

    // 1. Select Candidates
    Neighbor self(ip_addr, this);
    self.id = ntohl(inet_addr(THIS_ROUTER_ID));
    self.designated_router = designated_router;
    self.backup_designated_router = backup_designated_router;
    candidates.emplace_back(&self);

    for (auto& neighbor : neighbors) {
        if (static_cast<uint8_t>(neighbor.second->state) >= static_cast<uint8_t>(Neighbor::State::TWOWAY) && 
            neighbor.second->priority != 0) {
            candidates.emplace_back(neighbor.second);
        }
    }

    // 2. Elect DR and BDR
    Neighbor *dr = nullptr;
    Neighbor *bdr = nullptr;

    // 2.1 Elect BDR
    std::vector<Neighbor *> bdr_candidates_lv1;
    std::vector<Neighbor *> bdr_candidates_lv2;
    for (auto& candidate : candidates) {
        if (candidate->designated_router != candidate->ip_addr) {
            bdr_candidates_lv2.emplace_back(candidate);
            if (candidate->backup_designated_router == candidate->ip_addr) {
                bdr_candidates_lv1.emplace_back(candidate);
            }
        }
    }

    auto neighbor_cmp = [](Neighbor *a, Neighbor *b) {
        if (a->priority != b->priority) {
            return a->priority > b->priority;
        } else {
            return a->id > b->id;
        }
    };
    if (!bdr_candidates_lv1.empty()) {
        bdr = *std::max_element(bdr_candidates_lv1.begin(), bdr_candidates_lv1.end(), neighbor_cmp);
    } else if (!bdr_candidates_lv2.empty()) {
        bdr = *std::max_element(bdr_candidates_lv2.begin(), bdr_candidates_lv2.end(), neighbor_cmp);
    } // lv2 must be not empty

    // 2.2 Elect DR
    std::vector<Neighbor *> dr_candidates;
    for (auto& candidate : candidates) {
        if (candidate->designated_router == candidate->ip_addr) {
            dr_candidates.emplace_back(candidate);
        }
    }
    if (!dr_candidates.empty()) {
        dr = *std::max_element(dr_candidates.begin(), dr_candidates.end(), neighbor_cmp);
    } // must be not empty

    designated_router = dr->ip_addr;
    backup_designated_router = bdr->ip_addr;

    // printf("\tnew DR: %x\n", designated_router);
    // printf("\tnew BDR: %x\n", backup_designated_router);
    // printf("Electing finished.\n");
    std::cout << "\tnew DR: " << ip_to_string(designated_router) << std::endl;
    std::cout << "\tnew BDR: " << ip_to_string(backup_designated_router) << std::endl;
    std::cout << "Electing finished." << std::endl;
}

// P2P/P2MP/VIRTUAL : State::DOWN -> State::POINT2POINT
// BROADCAST/NBMA : State::DOWN -> State::WAITING
void Interface::event_interface_up() {
    assert(state == State::DOWN);
    std::cout << "Interface " << ip_to_string(ip_addr) << " received interface_up:"
              << "\tstate " << state_names[(int)state] << " -> ";
    switch (type) {
    case Type::P2P:
    case Type::P2MP:
    case Type::VIRTUAL:
        state = State::POINT2POINT;
        break;
    case Type::BROADCAST:
    case Type::NBMA:
        state = State::WAITING;
        break;
    default:
        break;
    }
    std::cout << state_names[(int)state] << std::endl;
}

// State::WAITING -> State::DR/BACKUP/DROTHER
void Interface::event_wait_timer() {
    assert(state == State::WAITING);
    elect_designated_router();
    std::cout << "Interface " << ip_to_string(ip_addr) << " received wait_timer:"
              << "\tstate " << state_names[(int)state] << " -> ";
    if (ip_addr == designated_router) {
        state = State::DR;
    } else if (ip_addr == backup_designated_router) {
        state = State::BACKUP;
    } else {
        state = State::DROTHER;
    }
    std::cout << state_names[(int)state] << std::endl;
}

void Interface::event_backup_seen() {
    assert(state == State::WAITING);
    elect_designated_router();
    std::cout << "Interface " << ip_to_string(ip_addr) << " received backup_seen:"
              << "\tstate " << state_names[(int)state] << " -> ";
    if (ip_addr == designated_router) {
        state = State::DR;
    } else if (ip_addr == backup_designated_router) {
        state = State::BACKUP;
    } else {
        state = State::DROTHER;
    }
    std::cout << state_names[(int)state] << std::endl;
}

void Interface::event_neighbor_change() {
    assert(state == State::DR || state == State::BACKUP || state == State::DROTHER);
    elect_designated_router();
    std::cout << "Interface " << ip_to_string(ip_addr) << " received neighbor_change:"
              << "\tstate " << state_names[(int)state] << " -> ";
    if (ip_addr == designated_router) {
        state = State::DR;
    } else if (ip_addr == backup_designated_router) {
        state = State::BACKUP;
    } else {
        state = State::DROTHER;
    }
    std::cout << state_names[(int)state] << std::endl;
}

// Any -> State::LOOPBACK
void Interface::event_loop_ind() {
    std::cout << "Interface " << ip_to_string(ip_addr) << " received loop_ind:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::LOOPBACK;
    std::cout << state_names[(int)state] << std::endl;
}

// State::LOOPBACK -> State::DOWN
void Interface::event_unloop_ind() {
    assert(state == State::LOOPBACK);
}

// Any -> State::DOWN  
void Interface::event_interface_down() {
    std::cout << "Interface " << ip_to_string(ip_addr) << " received interface_down:"
              << "\tstate " << state_names[(int)state] << " -> ";
    state = State::DOWN;
    std::cout << state_names[(int)state] << std::endl;
}

Neighbor *Interface::add_neighbor(in_addr_t ip) {
    if (neighbors.find(ip) == neighbors.end()) {
        neighbors[ip] = new Neighbor(ip, this);
    }
    return neighbors[ip];
}

void Interface::remove_neighbor(in_addr_t ip) {
    if (neighbors.find(ip) != neighbors.end()) {
        delete neighbors[ip];
        neighbors.erase(ip);
    }
}

void Interface::clear_neighbors() {
    for (auto& neighbor : neighbors) {
        delete neighbor.second;
    }
    neighbors.clear();
}

Neighbor *Interface::get_neighbor(in_addr_t ip) {
    if (neighbors.find(ip) == neighbors.end()) {
        return nullptr;
    }
    return neighbors[ip];
}