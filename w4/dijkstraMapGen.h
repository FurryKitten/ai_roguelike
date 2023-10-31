#pragma once
#include <vector>
#include <flecs.h>

namespace dmaps
{
  void gen_player_approach_map(flecs::world &ecs, std::vector<float> &map);
  void gen_player_flee_map(flecs::world &ecs, std::vector<float> &map);
  void gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map);
  void gen_low_hp_map(flecs::world &ecs, std::vector<float> &map, flecs::entity &e, float hp);
  void gen_player_radius_approach_map(flecs::world &ecs, std::vector<float> &map, float radius);
};

