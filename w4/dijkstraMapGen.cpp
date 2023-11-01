#include "dijkstraMapGen.h"
#include "ecsTypes.h"
#include "dungeonUtils.h"
#include "aiUtils.h"

template<typename Callable>
static void query_dungeon_data(flecs::world &ecs, Callable c)
{
  static auto dungeonDataQuery = ecs.query<const DungeonData>();

  dungeonDataQuery.each(c);
}

template<typename Callable>
static void query_characters_positions(flecs::world &ecs, Callable c)
{
  static auto characterPositionQuery = ecs.query<const Position, const Team>();

  characterPositionQuery.each(c);
}

constexpr float invalid_tile_value = 1e5f;

static void init_tiles(std::vector<float> &map, const DungeonData &dd)
{
  map.resize(dd.width * dd.height);
  for (float &v : map)
    v = invalid_tile_value;
}

// scan version, could be implemented as Dijkstra version as well
static void process_dmap(std::vector<float> &map, const DungeonData &dd)
{
  bool done = false;
  auto getMapAt = [&](size_t x, size_t y, float def)
  {
    if (x < dd.width && y < dd.width && dd.tiles[y * dd.width + x] == dungeon::floor)
      return map[y * dd.width + x];
    return def;
  };
  auto getMinNei = [&](size_t x, size_t y)
  {
    float val = map[y * dd.width + x];
    val = std::min(val, getMapAt(x - 1, y + 0, val));
    val = std::min(val, getMapAt(x + 1, y + 0, val));
    val = std::min(val, getMapAt(x + 0, y - 1, val));
    val = std::min(val, getMapAt(x + 0, y + 1, val));
    return val;
  };
  while (!done)
  {
    done = true;
    for (size_t y = 0; y < dd.height; ++y)
      for (size_t x = 0; x < dd.width; ++x)
      {
        const size_t i = y * dd.width + x;
        if (dd.tiles[i] != dungeon::floor)
          continue;
        const float myVal = getMapAt(x, y, invalid_tile_value);
        const float minVal = getMinNei(x, y);
        if (minVal < myVal - 1.f)
        {
          map[i] = minVal + 1.f;
          done = false;
        }
      }
  }
}

void dmaps::gen_player_approach_map(flecs::world &ecs, std::vector<float> &map)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      if (t.team == 0) // player team hardcode
        map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd);
  });
}

void dmaps::gen_player_flee_map(flecs::world &ecs, std::vector<float> &map)
{
  gen_player_approach_map(ecs, map);
  for (float &v : map)
    if (v < invalid_tile_value)
      v *= -1.2f;
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    process_dmap(map, dd);
  });
}

void dmaps::gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map)
{
  static auto hiveQuery = ecs.query<const Position, const Hive>();
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    hiveQuery.each([&](const Position &pos, const Hive &)
    {
      map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd);
  });
}

void dmaps::gen_low_hp_map(flecs::world &ecs, std::vector<float> &map, flecs::entity &e, float hp)
{
  static auto friendQuery = ecs.query<const Position, const Team>();
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    e.get([&](const Position& my_pos, const Team& my_team, const Hitpoints &health){
      friendQuery.each([&](const Position &pos, const Team & friend_team)
      {
        if (my_pos != pos && my_team.team == friend_team.team && health.hitpoints < hp) {
          map[pos.y * dd.width + pos.x] = 0.f;
        }
      });
    });
    process_dmap(map, dd);
  });
}

static bool is_tile_reachable(const DungeonData &dd, int x, int y, int dest_x, int dest_y, int max_dist)
{
  Position curPos{x, y};
  Position destPos{dest_x, dest_y};
  int range = abs(curPos.x - dest_x) + abs(curPos.y - dest_y);
  if (range != max_dist)
    return false;
  while (curPos != destPos)
  {
    curPos = move_pos(curPos, move_towards(curPos, destPos));
    if (dd.tiles[size_t(curPos.y) * dd.width + size_t(curPos.x)] == dungeon::wall)
      return false;
  }
  return true;
}

static int get_range(int x, int y, int dest_x, int dest_y)
{
  return abs(x - dest_x) + abs(y - dest_y);
}

static bool is_tile_in_range(int x, int y, int dest_x, int dest_y, int max_dist)
{
  return abs(x - dest_x) + abs(y - dest_y) <= max_dist;
}

void dmaps::gen_player_radius_approach_map(flecs::world &ecs, std::vector<float> &map, float radius)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      if (t.team == 0)
      {
        for(int dy = -radius; dy <= radius; dy++)
          for (int dx = -radius; dx <= radius; dx++)
          {
            int px = pos.x + dx;
            int py = pos.y + dy;
            if (px < 0 || px >= int(dd.width) ||
                py < 0 || py >= int(dd.height) ||
                !is_tile_reachable(dd, pos.x, pos.y, px, py, radius))
            {
              continue;
            }
            map[py * dd.width + px] = 0.f;
          }
      }
    });
    process_dmap(map, dd);
  });
}

void dmaps::gen_explore_map(flecs::world &ecs, std::vector<float> &map)
{
  static auto playerQuery = ecs.query<const Position, ExploreMap>();

  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    int closest_x, closest_y, closest_dist = 1e3;
    init_tiles(map, dd);
    playerQuery.each([&](const Position& pos, ExploreMap& exploreMap){
     for (int y = 0; y < dd.height; ++y)
      for (int x = 0; x < dd.width; ++x)
      {
        if (dd.tiles[y * dd.width + x] != dungeon::floor)
          continue;
        if (is_tile_in_range(pos.x, pos.y, x, y, exploreMap.dist))
        {
          exploreMap.explored[y * dd.width + x] = true;
        }
        int curDist = get_range(pos.x, pos.y, x, y);
        if (!exploreMap.explored[y * dd.width + x] && curDist < closest_dist)
        {
          closest_x = x;
          closest_y = y;
          closest_dist = curDist;
        }
      }
      map[closest_y * dd.width + closest_x] = 0.f;
    });
    process_dmap(map, dd);
  });
}
