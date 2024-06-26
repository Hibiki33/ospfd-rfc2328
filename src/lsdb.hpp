#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <list>
#include <mutex>
#include <netinet/in.h>
#include <thread>

#include "packet.hpp"

class Interface;

class LSDB {
public:
    std::list<RouterLSA *> router_lsas;
    std::list<NetworkLSA *> network_lsas;
    std::list<SummaryLSA *> summary_lsas;
    std::list<ASBRSummaryLSA *> asbr_summary_lsas;
    std::list<ASExternalLSA *> as_external_lsas;

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
        for (auto& lsa : asbr_summary_lsas) {
            delete lsa;
        }
        for (auto& lsa : as_external_lsas) {
            delete lsa;
        }
        // router_lsas.clear();
        // network_lsas.clear();
        // summary_lsas.clear();
        // asbr_summary_lsas.clear();
        // as_external_lsas.clear();
    }

    RouterLSA *get_router_lsa(uint32_t ls_id);
    RouterLSA *get_router_lsa(uint32_t ls_id, uint32_t adv_rtr);
    NetworkLSA *get_network_lsa(uint32_t ls_id);
    NetworkLSA *get_network_lsa(uint32_t ls_id, uint32_t adv_rtr);

    void add(LSA::Base *lsa) noexcept;
    void del(LSA::Type type, uint32_t ls_id, uint32_t adv_rtr) noexcept;
    LSA::Base *get(LSA::Type type, uint32_t ls_id, uint32_t adv_rtr) noexcept;

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

public:
    void make_lsa(LSA::Type type, Interface *interface = nullptr) noexcept;
};

extern LSDB this_lsdb;

static inline void MAKE_ROUTER_LSA(Interface *interface) {
    this_lsdb.lock();
    this_lsdb.make_lsa(LSA::Type::ROUTER, interface);
    this_lsdb.unlock();
}

static inline void MAKE_NETWORK_LSA(Interface *interface) {
    this_lsdb.lock();
    this_lsdb.make_lsa(LSA::Type::NETWORK, interface);
    this_lsdb.unlock();
}