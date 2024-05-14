#include <iostream>

#include "neighbor.hpp"

std::unordered_map<Neighbor::State, std::unordered_map<Neighbor::Event, Neighbor::State>>
    Neighbor::nsm = {{State::DOWN,
                      {
                          {Event::START, State::ATTEMPT},
                          {Event::HELLO_RECEIVED, State::INIT},
                      }},
                     {State::INIT,
                      {
                          {Event::TWOWAY_RECEIVED, State::TWOWAY},
                      }},
                     {State::TWOWAY,
                      {
                          {Event::ONEWAY_RECEIVED, State::INIT},
                          {Event::ADJ_OK, State::EXSTART},
                      }},
                     {State::EXSTART,
                      {
                          {Event::NEGOTIATION_DONE, State::EXCHANGE},
                      }},
                     {State::EXCHANGE,
                      {
                          {Event::EXCHANGE_DONE, State::LOADING},
                      }},
                     {State::LOADING,
                      {
                          {Event::LOADING_DONE, State::FULL},
                      }},
                     {State::FULL, {}}};

const char *Neighbor::state_str[] = {"DOWN",    "ATTEMPT",  "INIT",    "TWOWAY",
                                     "EXSTART", "EXCHANGE", "LOADING", "FULL"};

const char *Neighbor::event_str[] = {"HELLO_RECEIVED",   "START",         "TWOWAY_RECEIVED",
                                     "NEGOTIATION_DONE", "EXCHANGE_DONE", "BAD_LSREQ",
                                     "LOADING_DONE",     "ADJ_OK",        "SEQ_NUM_MISMATCH",
                                     "ONEWAY_RECEIVED",  "KILL_NBR",      "INACTIVITY_TIMER",
                                     "LL_DOWN"};

void Neighbor::handle_event(Event event) {
    printf("Neighbor %x received event %s ", id, event_str[static_cast<int>(event)]);
    auto new_state_it = nsm[state].find(event);
    if (new_state_it == nsm[state].end()) {
        printf("and rejected.\n");
        return;
    }
    auto new_state = new_state_it->second;
    state = new_state;
    printf("and its state from %s -> %s.\n", state_str[static_cast<int>(state)],
           state_str[static_cast<int>(new_state)]);
}