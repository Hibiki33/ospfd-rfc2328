#include <algorithm>

#include "interface.hpp"
#include "lsdb.hpp"
#include "transit.hpp"

LSDB this_lsdb;

RouterLSA *LSDB::get_router_lsa(uint32_t ls_id, uint32_t adv_rtr) {
    auto it = std::find_if(router_lsas.begin(), router_lsas.end(), [ls_id, adv_rtr](RouterLSA *rlsa) {
        return rlsa->header.link_state_id == ls_id && rlsa->header.advertising_router == adv_rtr;
    });
    return it != router_lsas.end() ? *it : nullptr;
}

RouterLSA *LSDB::get_router_lsa(uint32_t ls_id) {
    auto it = std::find_if(router_lsas.begin(), router_lsas.end(),
                           [ls_id](RouterLSA *rlsa) { return rlsa->header.link_state_id == ls_id; });
    return it != router_lsas.end() ? *it : nullptr;
}

NetworkLSA *LSDB::get_network_lsa(uint32_t ls_id, uint32_t adv_rtr) {
    auto it = std::find_if(network_lsas.begin(), network_lsas.end(), [ls_id, adv_rtr](NetworkLSA *nlsa) {
        return nlsa->header.link_state_id == ls_id && nlsa->header.advertising_router == adv_rtr;
    });
    return it != network_lsas.end() ? *it : nullptr;
}

NetworkLSA *LSDB::get_network_lsa(uint32_t ls_id) {
    auto it = std::find_if(network_lsas.begin(), network_lsas.end(),
                           [ls_id](NetworkLSA *nlsa) { return nlsa->header.link_state_id == ls_id; });
    return it != network_lsas.end() ? *it : nullptr;
}

void LSDB::add_lsa(char *net_ptr) {
    auto hdr = reinterpret_cast<LSA::Header *>(net_ptr);

    if (hdr->type == LSA::Type::ROUTER) {
        auto rlsa = new RouterLSA(net_ptr);
        mtx.lock();
        auto check_it = std::find_if(router_lsas.begin(), router_lsas.end(),
                                     [rlsa](RouterLSA *r) { return &r->header == &rlsa->header; });
        if (check_it != router_lsas.end()) {
            delete *check_it;
            router_lsas.erase(check_it);
        }
        router_lsas.emplace_back(rlsa);
        mtx.unlock();

    } else if (hdr->type == LSA::Type::NETWORK) {
        auto nlsa = new NetworkLSA(net_ptr);

        mtx.lock();
        auto check_it = std::find_if(network_lsas.begin(), network_lsas.end(),
                                     [nlsa](NetworkLSA *n) { return &n->header == &nlsa->header; });
        if (check_it != network_lsas.end()) {
            delete *check_it;
            network_lsas.erase(check_it);
        }
        network_lsas.emplace_back(nlsa);
        mtx.unlock();
    }
}

void LSDB::remove_lsa(uint32_t ls_id, uint32_t adv_rtr, LSA::Type type) {
    if (type == LSA::Type::ROUTER) {
        mtx.lock();
        auto it = std::find_if(router_lsas.begin(), router_lsas.end(), [ls_id, adv_rtr](RouterLSA *rlsa) {
            return rlsa->header.link_state_id == ls_id && rlsa->header.advertising_router == adv_rtr;
        });
        if (it != router_lsas.end()) {
            delete *it;
            router_lsas.erase(it);
        }
        mtx.unlock();

    } else if (type == LSA::Type::NETWORK) {
        mtx.lock();
        auto it = std::find_if(network_lsas.begin(), network_lsas.end(), [ls_id, adv_rtr](NetworkLSA *nlsa) {
            return nlsa->header.link_state_id == ls_id && nlsa->header.advertising_router == adv_rtr;
        });
        if (it != network_lsas.end()) {
            delete *it;
            network_lsas.erase(it);
        }
        mtx.unlock();
    }
}

void LSDB::flood_lsa(LSA::Base *lsa, std::vector<Interface *>& sel_interfaces) {
    char data[1024];
    memset(data, 0, 1024);

    auto lsu = reinterpret_cast<OSPF::LSU *>(data);
    lsu->num_lsas = htons(1);

    // auto lsa_packet = lsa->to_packet();
    // memcpy(lsu + sizeof(OSPF::LSU), lsa_packet, lsa->size());
    // delete[] lsa_packet;
    lsa->to_packet(reinterpret_cast<char *>(lsu + sizeof(OSPF::LSU)));

    for (auto& interface : sel_interfaces) {
        if (interface->state == Interface::State::DROTHER || interface->state == Interface::State::BACKUP ||
            interface->state == Interface::State::POINT2POINT) {
            // TODO: 目的ip完善
            send_packet(data, sizeof(OSPF::LSU) + lsa->size(), OSPF::Type::LSU, 0, interface);
        }
    }
}