#pragma once

#include "stateMachine.h"

// states
State *create_attack_enemy_state();
State *create_move_to_enemy_state();
State *create_flee_from_enemy_state();
State *create_patrol_state(float patrol_dist);
State *create_nop_state();

State *create_heal_state(float patrol_dist);
State *create_heal_player_state(float healingDist, float healAmount, int cooldownTime);
template <typename Tag> State *create_move_to_tagged_state();
template <typename Tag> State *create_patrol_tagged_state(float dist);

// transitions
StateTransition *create_enemy_available_transition(float dist);
StateTransition *create_enemy_reachable_transition();
StateTransition *create_hitpoints_less_than_transition(float thres);
StateTransition *create_negate_transition(StateTransition *in);
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs);

StateTransition *create_cooldown_transition();
StateTransition *create_player_hp_less_transition(float hp);

