#include "aiLibrary.h"
#include <flecs.h>
#include "ecsTypes.h"
#include "raylib.h"
#include <cfloat>
#include <cmath>

class AttackEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &/*ecs*/, flecs::entity /*entity*/) override {}
};

template<typename T>
T sqr(T a){ return a*a; }

template<typename T, typename U>
static float dist_sq(const T &lhs, const U &rhs) { return float(sqr(lhs.x - rhs.x) + sqr(lhs.y - rhs.y)); }

template<typename T, typename U>
static float dist(const T &lhs, const U &rhs) { return sqrtf(dist_sq(lhs, rhs)); }

template<typename T, typename U>
static int move_towards(const T &from, const U &to)
{
  int deltaX = to.x - from.x;
  int deltaY = to.y - from.y;
  if (abs(deltaX) > abs(deltaY))
    return deltaX > 0 ? EA_MOVE_RIGHT : EA_MOVE_LEFT;
  return deltaY < 0 ? EA_MOVE_UP : EA_MOVE_DOWN;
}

static int inverse_move(int move)
{
  return move == EA_MOVE_LEFT ? EA_MOVE_RIGHT :
         move == EA_MOVE_RIGHT ? EA_MOVE_LEFT :
         move == EA_MOVE_UP ? EA_MOVE_DOWN :
         move == EA_MOVE_DOWN ? EA_MOVE_UP : move;
}


template<typename Callable>
static void on_closest_enemy_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto enemiesQuery = ecs.query<const Position, const Team>();
  entity.set([&](const Position &pos, const Team &t, Action &a)
  {
    flecs::entity closestEnemy;
    float closestDist = FLT_MAX;
    Position closestPos;
    enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
    {
      if (t.team == et.team)
        return;
      float curDist = dist(epos, pos);
      if (curDist < closestDist)
      {
        closestDist = curDist;
        closestPos = epos;
        closestEnemy = enemy;
      }
    });
    if (ecs.is_valid(closestEnemy))
      c(a, pos, closestPos);
  });
}

template<typename Tag, typename Callable>
static void on_closest_tagged_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto targets = ecs.query<const Position, const Tag>();
  entity.set([&](const Position &pos, Action &a)
  {
    flecs::entity closestTarget;
    float closestDist = FLT_MAX;
    Position closestPos;
    targets.each([&](flecs::entity enemy, const Position &epos, const Tag &tag)
    {
      float curDist = dist(epos, pos);
      if (curDist < closestDist)
      {
        closestDist = curDist;
        closestPos = epos;
        closestTarget = enemy;
      }
    });
    if (ecs.is_valid(closestTarget))
      c(a, pos, closestPos);
  });
}

class LootResourcesState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    auto chestQuery = ecs.query<const Position&, Chest&>();
    entity.set([&](const Position &pos, Craft &craft)
    {
      chestQuery.each([&](const Position& chest_pos, Chest& chest)
      {
        if (dist(chest_pos, pos) > 2.f) return;
        if (chest.resources > 0) {
          --chest.resources;
          ++craft.resources;
        }
        if (craft.craftedItems > 0) {
          --craft.craftedItems;
          ++chest.items;
        }
      });
    });
  }
};

class CraftState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    entity.set([&](const Position &pos, Craft &craft)
    {
      if (craft.resources > 0 || craft.craftedItems >= craft.itemsToCraft)
      {
        ++craft.craftedItems;
        --craft.resources;
      }
    });
  }
};

class SleepState : public State
{
  int sleepTime;
public:
  SleepState(int sleep) : sleepTime(sleep){}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override {
    auto chestQuery = ecs.query<Chest&>();
    entity.set([&](Cooldown& cooldown)
    {
      if (cooldown.time <= 0)
      {
        cooldown.time = sleepTime;
      
        chestQuery.each([&](Chest& chest)
        {
          chest.items = 0;
        });
      }
    });
  }
};

template <typename Tag>
class MoveToTaggedState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    on_closest_tagged_pos<Tag>(ecs, entity, [&](Action &a, const Position &pos, const Position &tagged_pos)
    {
      a.action = move_towards(pos, tagged_pos);
    });
  }
};

template <typename Tag>
class PatrolTaggedState : public State
{
  float patrolDist;
public:
  PatrolTaggedState(float dist) : patrolDist(dist) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    on_closest_tagged_pos<Tag>(ecs, entity, [&](Action &a, const Position &player_pos, const Position &tagged_pos)
    {
      entity.set([&](const Position &pos, const PatrolPos &ppos, Action &a)
      {
        if (dist(tagged_pos, pos) > patrolDist)
        {
          a.action = move_towards(pos, tagged_pos);
        }
        else
        {
          a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1);
        }
      });
    });
  }
};

class HealPlayerState : public State
{
  float healingDist;
  float healAmount;
  int cooldownTime;
public:
  HealPlayerState(float dist, float heal, int cooldown) : healingDist(dist), healAmount(heal), cooldownTime(cooldown) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    auto playerQuery = ecs.query<Hitpoints&>();
    entity.set([&](const Position &pos, const PatrolPos &ppos, Action &a, Cooldown& cooldown)
    {
      if (dist(pos, ppos) > healingDist)
        a.action = move_towards(pos, ppos);
      else
      {
        playerQuery.each([&](Hitpoints &health)
        {
          health.hitpoints += healAmount;
        });
        cooldown.time = cooldownTime;
      }
    });
  }
};

class MoveToEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = move_towards(pos, enemy_pos);
    });
  }
};

class FleeFromEnemyState : public State
{
public:
  FleeFromEnemyState() {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = inverse_move(move_towards(pos, enemy_pos));
    });
  }
};

class PatrolState : public State
{
  float patrolDist;
public:
  PatrolState(float dist) : patrolDist(dist) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    entity.set([&](const Position &pos, const PatrolPos &ppos, Action &a)
    {
      if (dist(pos, ppos) > patrolDist)
        a.action = move_towards(pos, ppos); // do a recovery walk
      else
      {
        // do a random walk
        a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1);
      }
    });
  }
};

class HealState : public State
{
  float healAmount;
public:
  HealState(float amount) : healAmount(amount) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override
  {
    entity.set([&](const Position &pos, Hitpoints &health, Action &a)
    {
      health.hitpoints += healAmount;
    });
  }
};

class NopState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) override {}
};

class EnemyAvailableTransition : public StateTransition
{
  float triggerDist;
public:
  EnemyAvailableTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    bool enemiesFound = false;
    entity.get([&](const Position &pos, const Team &t)
    {
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        enemiesFound |= curDist <= triggerDist;
      });
    });
    return enemiesFound;
  }
};

template <typename Tag>
class NearTargetTransition : public StateTransition
{
  float triggerDist;
public:
  NearTargetTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto query = ecs.query<const Position, const Tag>();
    bool isNear = false;
    entity.get([&](const Position &pos)
    {
      query.each([&](flecs::entity enemy, const Position &target_pos, const Tag &tag)
      {
        float curDist = dist(target_pos, pos);
        isNear |= curDist <= triggerDist;
      });
    });
    return isNear;
  }
};


class LootedResourcesTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool isLooted = false;
    auto chestQuery = ecs.query<const Position&, const Chest&>();
    entity.get([&](const Position& pos, const Craft& craft)
    {
      isLooted |= craft.resources >= craft.itemsToCraft;
      chestQuery.each([&](const Position& chest_pos, const Chest& chest)
      {
        if (dist(chest_pos, pos) > 2.f) return;
        isLooted |= chest.resources <= 0;
      });
    });
    return isLooted;
  }
};

class CraftedEnoughTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool isCrafted = false;
    entity.get([&](const Craft& craft)
    {
      isCrafted |= craft.craftedItems >= craft.itemsToCraft;
    });
    return isCrafted;
  }
};

class FilledChestTransition : public StateTransition
{
  int itemsThreshold;
public:
  FilledChestTransition(int threshold) : itemsThreshold(threshold) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool isFilled = false;
    auto chestQuery = ecs.query<const Position&, const Chest&>();
    entity.get([&](const Position& pos, const Craft& craft)
    {
      chestQuery.each([&](const Position& chest_pos, const Chest& chest)
      {
        if (dist(chest_pos, pos) > 2.f) return;
        isFilled |= chest.items >= itemsThreshold;
      });
    });
    return isFilled;
  }
};

class HitpointsLessThanTransition : public StateTransition
{
  float threshold;
public:
  HitpointsLessThanTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool hitpointsThresholdReached = false;
    entity.get([&](const Hitpoints &hp)
    {
      hitpointsThresholdReached |= hp.hitpoints < threshold;
    });
    return hitpointsThresholdReached;
  }
};


class PlayerHPLessThanTransition : public StateTransition
{
  float threshold;
public:
  PlayerHPLessThanTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    auto playerQuery = ecs.query<Hitpoints&>();
    bool hitpointsThresholdReached = false;
    playerQuery.each([&](Hitpoints &health)
    {
      hitpointsThresholdReached |= health.hitpoints < threshold;
    });
    return hitpointsThresholdReached;
  }
};

class CooldownReadyTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool cooldownReady = false;
    entity.get([&](const Cooldown &cd)
    {
      cooldownReady |= cd.time <= 0;
    });
    return cooldownReady;
  }
};

class EnemyReachableTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return false;
  }
};

class NegateTransition : public StateTransition
{
  const StateTransition *transition; // we own it
public:
  NegateTransition(const StateTransition *in_trans) : transition(in_trans) {}
  ~NegateTransition() override { delete transition; }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return !transition->isAvailable(ecs, entity);
  }
};

class AndTransition : public StateTransition
{
  const StateTransition *lhs; // we own it
  const StateTransition *rhs; // we own it
public:
  AndTransition(const StateTransition *in_lhs, const StateTransition *in_rhs) : lhs(in_lhs), rhs(in_rhs) {}
  ~AndTransition() override
  {
    delete lhs;
    delete rhs;
  }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return lhs->isAvailable(ecs, entity) && rhs->isAvailable(ecs, entity);
  }
};


// states
State *create_attack_enemy_state()
{
  return new AttackEnemyState();
}

State *create_move_to_enemy_state()
{
  return new MoveToEnemyState();
}

State *create_flee_from_enemy_state()
{
  return new FleeFromEnemyState();
}


State *create_patrol_state(float patrol_dist)
{
  return new PatrolState(patrol_dist);
}

State *create_nop_state()
{
  return new NopState();
}

// transitions
StateTransition *create_enemy_available_transition(float dist)
{
  return new EnemyAvailableTransition(dist);
}

StateTransition *create_enemy_reachable_transition()
{
  return new EnemyReachableTransition();
}

StateTransition *create_hitpoints_less_than_transition(float thres)
{
  return new HitpointsLessThanTransition(thres);
}

StateTransition *create_negate_transition(StateTransition *in)
{
  return new NegateTransition(in);
}
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs)
{
  return new AndTransition(lhs, rhs);
}


State *create_heal_state(float healAmount)
{
  return new HealState(healAmount);
}

State *create_heal_player_state(float healingDist, float healAmount, int cooldownTime)
{
  return new HealPlayerState(healingDist, healAmount, cooldownTime);
}

template <typename Tag>
State *create_move_to_tagged_state()
{
  return new MoveToTaggedState<Tag>();
}
template State *create_move_to_tagged_state<IsPlayer>();
template State *create_move_to_tagged_state<IsMonster>();
template State *create_move_to_tagged_state<CraftingTable>();
template State *create_move_to_tagged_state<Chest>();
template State *create_move_to_tagged_state<Bed>();

template <typename Tag>
State *create_patrol_tagged_state(float dist)
{
  return new PatrolTaggedState<Tag>(dist);
}
template State *create_patrol_tagged_state<IsPlayer>(float dist);


State *create_loot_resources_state()
{
  return new LootResourcesState();
}

State *create_craft_state()
{
  return new CraftState();
}


State *create_sleep_state(int time)
{
  return new SleepState(time);
}

StateTransition *create_cooldown_transition()
{
  return new CooldownReadyTransition();
}

StateTransition *create_player_hp_less_transition(float hp)
{
  return new PlayerHPLessThanTransition(hp);
}

StateTransition *create_crafted_enough_transition()
{
  return new CraftedEnoughTransition();
}

StateTransition *create_looted_enough_transition()
{
  return new LootedResourcesTransition();
}

StateTransition *create_filled_chest_transition(int items)
{
  return new FilledChestTransition(items);
}

template <typename Tag>
StateTransition *create_near_target_transition(float dist)
{
  return new NearTargetTransition<Tag>(dist);
}
template StateTransition *create_near_target_transition<CraftingTable>(float dist);
template StateTransition *create_near_target_transition<Chest>(float dist);
template StateTransition *create_near_target_transition<Bed>(float dist);