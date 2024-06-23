#include <algorithm>
#include <iostream>

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