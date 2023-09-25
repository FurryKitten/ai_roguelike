#pragma once

#include "stateMachine.h"

// states
State *create_attack_enemy_state();
State *create_move_to_enemy_state();
State *create_flee_from_enemy_state();
State *create_patrol_state(float patrol_dist);
State *create_nop_state();

State *create_heal_state(float heal_amount);
State *create_heal_player_state(float healingDist, float healAmount, int cooldownTime);
template <typename Tag> State *create_move_to_tagged_state();
template <typename Tag> State *create_patrol_tagged_state(float dist);

State *create_loot_resources_state();
State *create_craft_state();
State *create_sleep_state(int time);

// transitions
StateTransition *create_enemy_available_transition(float dist);
StateTransition *create_enemy_reachable_transition();
StateTransition *create_hitpoints_less_than_transition(float thres);
StateTransition *create_negate_transition(StateTransition *in);
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs);

StateTransition *create_cooldown_transition();
StateTransition *create_player_hp_less_transition(float hp);
StateTransition *create_crafted_enough_transition();
StateTransition *create_looted_enough_transition();
StateTransition *create_filled_chest_transition(int items);

template <typename Tag> StateTransition *create_near_target_transition(float dist);

