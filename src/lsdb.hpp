#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <netinet/in.h>
#include <thread>
#include <vector>

#include "packet.hpp"

class Interface;

class LSDB {
public:
    std::vector<RouterLSA *> router_lsas;
    std::vector<NetworkLSA *> network_lsas;
    std::vector<SummaryLSA *> summary_lsas; // 事实上，在本实验中不会被使用

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
        for (auto& lsa : summary_lsas) {
            delete lsa;
        }
        // router_lsas.clear();
        // network_lsas.clear();
        // summary_lsas.clear();
    }

    RouterLSA *get_router_lsa(uint32_t ls_id);
    RouterLSA *get_router_lsa(uint32_t ls_id, uint32_t adv_rtr);
    NetworkLSA *get_network_lsa(uint32_t ls_id);
    NetworkLSA *get_network_lsa(uint32_t ls_id, uint32_t adv_rtr);

    void add(LSA::Base *lsa) noexcept {
        switch (lsa->header.type) {
        case LSA::Type::ROUTER:
            router_lsas.emplace_back(static_cast<RouterLSA *>(lsa));
            break;
        case LSA::Type::NETWORK:
            network_lsas.emplace_back(static_cast<NetworkLSA *>(lsa));
            break;
        default:
            assert(false && "Not implemented yet");
            break;
        }
    }

    void del(uint32_t ls_id, uint32_t adv_rtr, LSA::Type type) noexcept {
        if (type == LSA::Type::ROUTER) {
            auto it = std::find_if(router_lsas.begin(), router_lsas.end(), [ls_id, adv_rtr](RouterLSA *rlsa) {
                return rlsa->header.link_state_id == ls_id && rlsa->header.advertising_router == adv_rtr;
            });
            if (it != router_lsas.end()) {
                delete *it;
                router_lsas.erase(it);
            }
        } else if (type == LSA::Type::NETWORK) {
            auto it = std::find_if(network_lsas.begin(), network_lsas.end(), [ls_id, adv_rtr](NetworkLSA *nlsa) {
                return nlsa->header.link_state_id == ls_id && nlsa->header.advertising_router == adv_rtr;
            });
            if (it != network_lsas.end()) {
                delete *it;
                network_lsas.erase(it);
            }
        } else {
            assert(false && "Not implemented yet");
        }
    }

    LSA::Base *get(uint32_t ls_id, uint32_t adv_rtr, LSA::Type type) noexcept {
        if (type == LSA::Type::ROUTER) {
            auto it = std::find_if(router_lsas.begin(), router_lsas.end(), [ls_id, adv_rtr](RouterLSA *rlsa) {
                return rlsa->header.link_state_id == ls_id && rlsa->header.advertising_router == adv_rtr;
            });
            return it != router_lsas.end() ? *it : nullptr;
        } else if (type == LSA::Type::NETWORK) {
            auto it = std::find_if(network_lsas.begin(), network_lsas.end(), [ls_id, adv_rtr](NetworkLSA *nlsa) {
                return nlsa->header.link_state_id == ls_id && nlsa->header.advertising_router == adv_rtr;
            });
            return it != network_lsas.end() ? *it : nullptr;
        } else {
            assert(false && "Not implemented yet");
            return nullptr;
        }
    }

    void lock() noexcept {
        mtx.lock();
    }
    void unlock() noexcept {
        mtx.unlock();
    }

    size_t lsa_num() const {
        return router_lsas.size() + network_lsas.size() + summary_lsas.size();
    }

private:
    std::mutex mtx; // 保护LSDB的互斥锁
};

extern LSDB this_lsdb;