#include <algorithm>
#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "interface.hpp"
#include "neighbor.hpp"

std::vector<Interface *> this_interfaces;

static const char *state_str[] = {"DOWN",    "LOOPBACK", "WAITING", "POINT2POINT",
                                  "DROTHER", "BACKUP",   "DR"};

void Interface::elect_designated_router() {
    printf("\n\tStart electing DR and BDR...\n");

    std::list<Neighbor *> candidates;

    // 1. Select Candidates
    Neighbor self(ip_addr, this);
    // TODO: init self
    candidates.emplace_back(&self);

    for (auto& neighbor : neighbors) {
        // TODO: compare neighbor state
    }

    // 2. Elect DR and BDR
    Neighbor *dr = nullptr;
    Neighbor *bdr = nullptr;

    // 2.1 Elect BDR
    std::vector<Neighbor *> bdr_candidates_lv1;
    std::vector<Neighbor *> bdr_candidates_lv2;
    for (auto& candidate : candidates) {
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
    }
    if (!dr_candidates.empty()) {
        dr = *std::max_element(dr_candidates.begin(), dr_candidates.end(), neighbor_cmp);
    } // must be not empty

    if (dr->ip_addr == ip_addr && designated_router != ip_addr) {
    }

    designated_router = dr->ip_addr;
    backup_designated_router = bdr->ip_addr;

    printf("\tNew DR: %x\n", designated_router);
    printf("\tNew BDR: %x\n", backup_designated_router);
    printf("\tElecting finished.\n");
}

void Interface::event_interface_up() {
}

void Interface::event_wait_timer() {
    printf("Interface %x received wait_timer ", ip_addr);
    if (state == State::WAITING) {
        // TODO: elect DR and BDR
        if (ip_addr == designated_router) {
            printf("and its state from WAITING -> %s.\n", state_str[(int)State::DR]);
            state = State::DR;
        } else if (ip_addr == backup_designated_router) {
            printf("and its state from WAITING -> %s.\n", state_str[(int)State::BACKUP]);
            state = State::BACKUP;
        } else {
            printf("and its state from WAITING -> %s.\n", state_str[(int)State::DROTHER]);
            state = State::DROTHER;
        }
    } else {
        printf("and rejected.\n");
    }
}

void Interface::event_backup_seen() {
    printf("Interface %x received backup_seen ", ip_addr);
    if (state == State::WAITING) {
        // TODO: elect DR and BDR
        if (ip_addr == designated_router) {
            printf("and its state from WAITING -> %s.\n", state_str[(int)State::DR]);
            state = State::DR;
        } else if (ip_addr == backup_designated_router) {
            printf("and its state from WAITING -> %s.\n", state_str[(int)State::BACKUP]);
            state = State::BACKUP;
        } else {
            printf("and its state from WAITING -> %s.\n", state_str[(int)State::DROTHER]);
            state = State::DROTHER;
        }
    } else {
        printf("and rejected.\n");
    }
}

void Interface::event_neighbor_change() {
    printf("Interface %x received neighbor_change ", ip_addr);
    if (state == State::DR || state == State::BACKUP || state == State::DROTHER) {
        // elect_designated_router();
        if (ip_addr == designated_router) {
            printf("and its state from %s -> %s.\n", state_str[(int)state],
                   state_str[(int)State::DR]);
            state = State::DR;
        } else if (ip_addr == backup_designated_router) {
            printf("and its state from %s -> %s.\n", state_str[(int)state],
                   state_str[(int)State::BACKUP]);
            state = State::BACKUP;
        } else {
            printf("and its state from %s -> %s.\n", state_str[(int)state],
                   state_str[(int)State::DROTHER]);
            state = State::DROTHER;
        }
    } else {
        printf("and rejected.\n");
    }
}

void Interface::event_loop_ind() {
    printf("Interface %x received loop_ind ", ip_addr);
    printf("and its state from %s -> %s.\n", state_str[(int)state],
           state_str[(int)State::LOOPBACK]);
    state = State::LOOPBACK;
}

void Interface::event_unloop_ind() {
    printf("Interface %x received unloop_ind ", ip_addr);
    if (state == State::LOOPBACK) {
        printf("and its state from LOOPBACK -> %s.\n", state_str[(int)State::DOWN]);
        state = State::DOWN;
    } else {
        printf("and rejected.\n");
    }
}

void Interface::event_interface_down() {
    printf("Interface %x received interface_down ", ip_addr);
    printf("and its state from %s -> %s.\n", state_str[(int)state], state_str[(int)State::DOWN]);
    state = State::DOWN;
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