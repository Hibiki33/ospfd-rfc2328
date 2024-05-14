#pragma once

#include <cstdint>
#include <mutex>
#include <netinet/in.h>
#include <thread>
#include <vector>

#include "packet.hpp"

class Interface;

class LSDB {
private:
    std::vector<RouterLSA *> router_lsas;
    std::mutex router_lsas_mutex;

    std::vector<NetworkLSA *> network_lsas;
    std::mutex network_lsas_mutex;

    uint16_t max_age = 3600;     // max time an lsa can survive, default 3600s
    uint16_t max_age_diff = 900; // max time an lsa flood the AS, default 900s

public:
    LSDB() noexcept = default;
    ~LSDB() {
        for (auto& lsa : router_lsas) {
            delete lsa;
        }
        for (auto& lsa : network_lsas) {
            delete lsa;
        }
    }

    RouterLSA *get_router_lsa(uint32_t ls_id);
    RouterLSA *get_router_lsa(uint32_t ls_id, uint32_t adv_rtr);
    NetworkLSA *get_network_lsa(uint32_t ls_id);
    NetworkLSA *get_network_lsa(uint32_t ls_id, uint32_t adv_rtr);

    void add_lsa(char *net_ptr);
    void remove_lsa(uint32_t ls_id, uint32_t adv_rtr, LSA::Type type);
    void flood_lsa(LSA::Base *lsa, std::vector<Interface *>& sel_interfaces);
};