#include <algorithm>
#include <iostream>

#include "interface.hpp"
#include "lsdb.hpp"
#include "neighbor.hpp"
#include "packet.hpp"
#include "route.hpp"
#include "transit.hpp"

LSDB this_lsdb;

void LSDB::add(LSA::Base *lsa) noexcept {
    LSA::Base *old_lsa = get(lsa->header.type, lsa->header.link_state_id, lsa->header.advertising_router);
    if (old_lsa != nullptr) {
        if (*lsa > *old_lsa) {
            del(lsa->header.type, lsa->header.link_state_id, lsa->header.advertising_router);
        }
    }
    switch (lsa->header.type) {
    case LSA::Type::ROUTER:
        router_lsas.emplace_back(static_cast<RouterLSA *>(lsa));
        break;
    case LSA::Type::NETWORK:
        network_lsas.emplace_back(static_cast<NetworkLSA *>(lsa));
        break;
    case LSA::Type::SUMMARY:
        summary_lsas.emplace_back(static_cast<SummaryLSA *>(lsa));
        break;
    case LSA::Type::ASBR_SUMMARY:
        asbr_summary_lsas.emplace_back(static_cast<ASBRSummaryLSA *>(lsa));
        break;
    case LSA::Type::AS_EXTERNAL:
        as_external_lsas.emplace_back(static_cast<ASExternalLSA *>(lsa));
        break;
    default:
        assert(false && "Not implemented yet");
        break;
    }
}

void LSDB::del(LSA::Type type, uint32_t ls_id, uint32_t adv_rtr) noexcept {
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
    } else if (type == LSA::Type::SUMMARY) {
        auto it = std::find_if(summary_lsas.begin(), summary_lsas.end(), [ls_id, adv_rtr](SummaryLSA *slsa) {
            return slsa->header.link_state_id == ls_id && slsa->header.advertising_router == adv_rtr;
        });
        if (it != summary_lsas.end()) {
            delete *it;
            summary_lsas.erase(it);
        }
    } else {
        assert(false && "Not implemented yet");
    }
}

LSA::Base *LSDB::get(LSA::Type type, uint32_t ls_id, uint32_t adv_rtr) noexcept {
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
    } else if (type == LSA::Type::SUMMARY) {
        auto it = std::find_if(summary_lsas.begin(), summary_lsas.end(), [ls_id, adv_rtr](SummaryLSA *slsa) {
            return slsa->header.link_state_id == ls_id && slsa->header.advertising_router == adv_rtr;
        });
        return it != summary_lsas.end() ? *it : nullptr;
    } else if (type == LSA::Type::ASBR_SUMMARY) {
        auto it = std::find_if(
            asbr_summary_lsas.begin(), asbr_summary_lsas.end(), [ls_id, adv_rtr](ASBRSummaryLSA *asbrslsa) {
                return asbrslsa->header.link_state_id == ls_id && asbrslsa->header.advertising_router == adv_rtr;
            });
        return it != asbr_summary_lsas.end() ? *it : nullptr;
    } else if (type == LSA::Type::AS_EXTERNAL) {
        auto it =
            std::find_if(as_external_lsas.begin(), as_external_lsas.end(), [ls_id, adv_rtr](ASExternalLSA *aselsa) {
                return aselsa->header.link_state_id == ls_id && aselsa->header.advertising_router == adv_rtr;
            });
        return it != as_external_lsas.end() ? *it : nullptr;
    }
    return nullptr;
}

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

std::atomic<size_t> lsa_seq_num(0x80000001); // 本地LSA序列号

static RouterLSA *make_router_lsa() noexcept {
    auto rlsa = new RouterLSA();

    // 构造header
    rlsa->header.age = 0;
    rlsa->header.options = 0x02;
    rlsa->header.type = LSA::Type::ROUTER;
    rlsa->header.link_state_id = ntohl(inet_addr(THIS_ROUTER_ID));
    rlsa->header.advertising_router = ntohl(inet_addr(THIS_ROUTER_ID));
    rlsa->header.length = 0; //
    rlsa->header.sequence_number = lsa_seq_num++;
    rlsa->header.checksum = 0; //

    // 构造第1类LSA
    for (auto& interface : this_interfaces) {
        if (interface->state == Interface::State::DOWN) {
            continue;
        }
        auto link = RouterLSA::Link();
        link.metric = interface->cost;
        link.tos = 0;
        if ((interface->state != Interface::State::WAITING) &&
            (interface->designated_router == interface->ip_addr ||
             interface->get_neighbor_by_ip(interface->designated_router)->state == Neighbor::State::FULL)) {
            // 成功连入网络后，变为transit link
            link.type = LSA::LinkType::TRANSIT;
            link.link_id = interface->designated_router;
            link.link_data = interface->ip_addr;
        } else {
            // 最开始生成的rlsa是stub link
            link.type = LSA::LinkType::STUB;
            link.link_id = interface->ip_addr & interface->mask;
            link.link_data = interface->mask;
        }
        rlsa->links.emplace_back(link);
    }

    rlsa->num_links = rlsa->links.size();
    rlsa->header.length = rlsa->size();
    rlsa->make_checksum();

    return rlsa;
}

static NetworkLSA *make_network_lsa(Interface *interface) noexcept {
    NetworkLSA *nlsa = new NetworkLSA();

    // 构造header
    nlsa->header.age = 0;
    nlsa->header.options = 0x02;
    nlsa->header.type = LSA::Type::NETWORK;
    nlsa->header.link_state_id = interface->ip_addr;
    nlsa->header.advertising_router = ntohl(inet_addr(THIS_ROUTER_ID));
    nlsa->header.length = 0; //
    nlsa->header.sequence_number = lsa_seq_num++;
    nlsa->header.checksum = 0; //

    // 构造第2类LSA
    nlsa->network_mask = interface->mask;
    for (auto& neighbor : interface->neighbors) {
        if (neighbor->state == Neighbor::State::FULL) {
            nlsa->attached_routers.emplace_back(neighbor->ip_addr);
        }
    }

    nlsa->header.length = nlsa->size();
    nlsa->make_checksum();

    return nlsa;
}

// 由调用者保证已锁
void LSDB::make_lsa(LSA::Type type, Interface *interface) noexcept {
    auto this_rid = ntohl(inet_addr(THIS_ROUTER_ID));

    if (type == LSA::Type::ROUTER) {
        auto rlsa = static_cast<RouterLSA *>(get(LSA::Type::ROUTER, this_rid, this_rid));
        if (rlsa == nullptr) {
            rlsa = make_router_lsa();
            router_lsas.emplace_back(rlsa);
            // 此时不洪泛，只在本地更新
        } else {
            auto new_rlsa = make_router_lsa();
            add(new_rlsa); // add中会将（可能存在的）旧LSA删除
            OSPF::flood_lsa(new_rlsa);
        }
    } else if (type == LSA::Type::NETWORK) {
        auto nlsa = static_cast<NetworkLSA *>(get(LSA::Type::NETWORK, interface->ip_addr, this_rid));
        if (nlsa == nullptr) {
            nlsa = make_network_lsa(interface);
            network_lsas.emplace_back(nlsa);
        } else {
            auto new_nlsa = make_network_lsa(interface);
            add(new_nlsa);
            OSPF::flood_lsa(new_nlsa);
        }
    } else {
        assert(false && "Not implemented yet");
    }
}