#include "roguelike.h"
#include "ecsTypes.h"
#include "raylib.h"
#include "stateMachine.h"
#include "aiLibrary.h"

static void add_crafter_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    /// Craft state
    auto craft_sm = new StateMachine{};
    int go_to_chest = craft_sm->addState(create_move_to_tagged_state<Chest>());
    int go_to_craft = craft_sm->addState(create_move_to_tagged_state<CraftingTable>());
    int craft = craft_sm->addState(create_craft_state());
    int loot = craft_sm->addState(create_loot_resources_state());
    craft_sm->addTransition(create_near_target_transition<Chest>(1.f), go_to_chest, loot);
    craft_sm->addTransition(create_near_target_transition<CraftingTable>(1.f), go_to_craft, craft);
    craft_sm->addTransition(create_crafted_enough_transition(), craft, go_to_chest);
    craft_sm->addTransition(create_looted_enough_transition(), loot, go_to_craft);

    /// Sleep state
    auto sleep_sm = new StateMachine{};
    int go_to_bed = sleep_sm->addState(create_move_to_tagged_state<Bed>());
    int sleep = sleep_sm->addState(create_sleep_state(10));
    sleep_sm->addTransition(create_near_target_transition<Bed>(1.f), go_to_bed, sleep);

    /// Protect state
    auto protect_sm = new StateMachine{};
    int move_to_enemy = protect_sm->addState(create_move_to_enemy_state());
    int heal = protect_sm->addState(create_heal_state(20.f));
    protect_sm->addTransition(create_hitpoints_less_than_transition(60.f), move_to_enemy, heal);
    protect_sm->addTransition(create_negate_transition(create_hitpoints_less_than_transition(60.f)), heal, move_to_enemy);

    int craft_sm_id = sm.addState(craft_sm);
    int sleep_sm_id = sm.addState(sleep_sm);
    int protect_sm_id = sm.addState(protect_sm);

    sm.addTransition(create_and_transition(create_near_target_transition<Bed>(1.f), create_cooldown_transition()), sleep_sm_id, craft_sm_id);
    sm.addTransition(create_filled_chest_transition(10), craft_sm_id, sleep_sm_id);
    sm.addTransition(create_enemy_available_transition(5.f), craft_sm_id, protect_sm_id);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(4.f)), protect_sm_id, craft_sm_id);
  });
}

static void add_berserk_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    int patrol = sm.addState(create_patrol_state(4.f));
    int moveToEnemy = sm.addState(create_move_to_enemy_state());

    sm.addTransition(create_enemy_available_transition(7.f), patrol, moveToEnemy);
    sm.addTransition(create_hitpoints_less_than_transition(60.f), patrol, moveToEnemy);

    sm.addTransition(create_and_transition(create_negate_transition(create_enemy_available_transition(7.f)), 
                     create_negate_transition(create_hitpoints_less_than_transition(60.f))), moveToEnemy, patrol);
  });
}

static void add_healer_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    int patrol = sm.addState(create_patrol_state(4.f));
    int heal = sm.addState(create_heal_state(10.f));

    sm.addTransition(create_hitpoints_less_than_transition(90.f), patrol, heal);
    sm.addTransition(create_negate_transition(create_hitpoints_less_than_transition(90.f)), heal, patrol);
  });
}

static void add_guardian_sm(flecs::entity entity)
{
  entity.get([&](StateMachine &sm)
  {
    int patrol_player = sm.addState(create_patrol_tagged_state<IsPlayer>(5.f));
    int heal_player = sm.addState(create_heal_player_state(5.0f, 20.0f, 10));
    int attack_enemy = sm.addState(create_move_to_tagged_state<IsMonster>());

    sm.addTransition(create_and_transition(create_player_hp_less_transition(100.f), create_cooldown_transition()), patrol_player, heal_player);
    sm.addTransition(create_negate_transition(create_cooldown_transition()), heal_player, patrol_player);
    sm.addTransition(create_enemy_available_transition(4.f), patrol_player, attack_enemy);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(5.f)), attack_enemy, patrol_player);
  });
}

static void add_patrol_attack_flee_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    int patrol = sm.addState(create_patrol_state(3.f));
    int moveToEnemy = sm.addState(create_move_to_enemy_state());
    int fleeFromEnemy = sm.addState(create_flee_from_enemy_state());

    sm.addTransition(create_enemy_available_transition(3.f), patrol, moveToEnemy);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(5.f)), moveToEnemy, patrol);

    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(60.f), create_enemy_available_transition(5.f)),
                     moveToEnemy, fleeFromEnemy);
    sm.addTransition(create_and_transition(create_hitpoints_less_than_transition(60.f), create_enemy_available_transition(3.f)),
                     patrol, fleeFromEnemy);

    sm.addTransition(create_negate_transition(create_enemy_available_transition(7.f)), fleeFromEnemy, patrol);
  });
}

static void add_patrol_flee_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    int patrol = sm.addState(create_patrol_state(3.f));
    int fleeFromEnemy = sm.addState(create_flee_from_enemy_state());

    sm.addTransition(create_enemy_available_transition(3.f), patrol, fleeFromEnemy);
    sm.addTransition(create_negate_transition(create_enemy_available_transition(5.f)), fleeFromEnemy, patrol);
  });
}

static void add_attack_sm(flecs::entity entity)
{
  entity.get([](StateMachine &sm)
  {
    sm.addState(create_move_to_enemy_state());
  });
}

static flecs::entity create_monster(flecs::world &ecs, int x, int y, Color color)
{
  return ecs.entity()
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(PatrolPos{x, y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .add<IsMonster>()
    .set(Color{color})
    .set(StateMachine{})
    .set(Team{1})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f});
}

static flecs::entity create_npc(flecs::world &ecs, int x, int y, Color color)
{
  return ecs.entity()
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .add<IsFriend>()
    .set(Color{color})
    .set(StateMachine{})
    .set(Team{0})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f})
    .set(Cooldown{0});
}

static flecs::entity create_crafter(flecs::world &ecs, int x, int y, Color color)
{
  return ecs.entity()
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .add<IsFriend>()
    .set(Color{color})
    .set(StateMachine{})
    .set(Team{0})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f})
    .set(Cooldown{0})
    .set(Craft{.resources = 0, .itemsToCraft = 10, .craftedItems = 0});
}

static flecs::entity create_player(flecs::world &ecs, int x, int y)
{
  return
  ecs.entity("player")
    .set(Position{x, y})
    .set(MovePos{x, y})
    .set(Hitpoints{100.f})
    .set(GetColor(0xeeeeeeff))
    .set(Action{EA_NOP})
    .add<IsPlayer>()
    .set(Team{0})
    .set(PlayerInput{})
    .set(NumActions{2, 0})
    .set(MeleeDamage{50.f});
}

static void create_heal(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(HealAmount{amount})
    .set(GetColor(0x44ff44ff));
}

static void create_powerup(flecs::world &ecs, int x, int y, float amount)
{
  ecs.entity()
    .set(Position{x, y})
    .set(PowerupAmount{amount})
    .set(Color{255, 255, 0, 255});
}

static void create_chest(flecs::world &ecs, int x, int y)
{
  ecs.entity()
    .set(Position{x, y})
    .set(Color{125, 200, 50, 255})
    .set(Chest{1000, 0});
}

static void create_crafting_table(flecs::world &ecs, int x, int y)
{
  ecs.entity()
    .set(Position{x, y})
    .add<CraftingTable>()
    .set(Color{125, 200, 125, 255});
}

static void create_bed(flecs::world &ecs, int x, int y)
{
  ecs.entity()
    .set(Position{x, y})
    .add<Bed>()
    .set(Color{250, 250, 250, 255});
}

static void register_roguelike_systems(flecs::world &ecs)
{
  ecs.system<PlayerInput, Action, const IsPlayer>()
    .each([&](PlayerInput &inp, Action &a, const IsPlayer)
    {
      bool left = IsKeyDown(KEY_LEFT);
      bool right = IsKeyDown(KEY_RIGHT);
      bool up = IsKeyDown(KEY_UP);
      bool down = IsKeyDown(KEY_DOWN);
      if (left && !inp.left)
        a.action = EA_MOVE_LEFT;
      if (right && !inp.right)
        a.action = EA_MOVE_RIGHT;
      if (up && !inp.up)
        a.action = EA_MOVE_UP;
      if (down && !inp.down)
        a.action = EA_MOVE_DOWN;
      inp.left = left;
      inp.right = right;
      inp.up = up;
      inp.down = down;
    });
  ecs.system<const Position, const Color>()
    .term<TextureSource>(flecs::Wildcard).not_()
    .each([&](const Position &pos, const Color color)
    {
      const Rectangle rect = {float(pos.x), float(pos.y), 1, 1};
      DrawRectangleRec(rect, color);
    });
  ecs.system<const Position, const Color>()
    .term<TextureSource>(flecs::Wildcard)
    .each([&](flecs::entity e, const Position &pos, const Color color)
    {
      const auto textureSrc = e.target<TextureSource>();
      DrawTextureQuad(*textureSrc.get<Texture2D>(),
          Vector2{1, 1}, Vector2{0, 0},
          Rectangle{float(pos.x), float(pos.y), 1, 1}, color);
    });
}


void init_roguelike(flecs::world &ecs)
{
  register_roguelike_systems(ecs);

  /* add_patrol_attack_flee_sm(create_monster(ecs, 5, 5, GetColor(0xee00eeff)));
  add_patrol_attack_flee_sm(create_monster(ecs, 10, -5, GetColor(0xee00eeff)));
  add_patrol_flee_sm(create_monster(ecs, -5, -5, GetColor(0x111111ff)));
  add_attack_sm(create_monster(ecs, -5, 5, GetColor(0x880000ff))); */

  add_berserk_sm(create_monster(ecs, 5, 5, GetColor(0x660000ff)));
  add_healer_sm(create_monster(ecs, 5, -5, GetColor(0x007700ff)));
  add_guardian_sm(create_npc(ecs, -5, -5, GetColor(0x001199ff)));
  add_crafter_sm(create_crafter(ecs, 0, -12, GetColor(0x660066ff)));

  auto player = create_player(ecs, 0, 0);

  create_powerup(ecs, 7, 7, 10.f);
  create_powerup(ecs, 10, -6, 10.f);
  create_powerup(ecs, 10, -4, 10.f);

  create_heal(ecs, -5, -5, 50.f);
  create_heal(ecs, -5, 5, 50.f);

  create_chest(ecs, 0, -11);
  create_crafting_table(ecs, 5, -12);
  create_bed(ecs, 10, -13);
}

static bool is_player_acted(flecs::world &ecs)
{
  static auto processPlayer = ecs.query<const IsPlayer, const Action>();
  bool playerActed = false;
  processPlayer.each([&](const IsPlayer, const Action &a)
  {
    playerActed = a.action != EA_NOP;
  });
  return playerActed;
}

static bool upd_player_actions_count(flecs::world &ecs)
{
  static auto updPlayerActions = ecs.query<const IsPlayer, NumActions>();
  bool actionsReached = false;
  updPlayerActions.each([&](const IsPlayer, NumActions &na)
  {
    na.curActions = (na.curActions + 1) % na.numActions;
    actionsReached |= na.curActions == 0;
  });
  return actionsReached;
}

static Position move_pos(Position pos, int action)
{
  if (action == EA_MOVE_LEFT)
    pos.x--;
  else if (action == EA_MOVE_RIGHT)
    pos.x++;
  else if (action == EA_MOVE_UP)
    pos.y--;
  else if (action == EA_MOVE_DOWN)
    pos.y++;
  return pos;
}

static void process_actions(flecs::world &ecs)
{
  static auto processActions = ecs.query<Action, Position, MovePos, const MeleeDamage, const Team>();
  static auto checkAttacks = ecs.query<const MovePos, Hitpoints, const Team>();
  // Process all actions
  ecs.defer([&]
  {
    processActions.each([&](flecs::entity entity, Action &a, Position &pos, MovePos &mpos, const MeleeDamage &dmg, const Team &team)
    {
      Position nextPos = move_pos(pos, a.action);
      bool blocked = false;
      checkAttacks.each([&](flecs::entity enemy, const MovePos &epos, Hitpoints &hp, const Team &enemy_team)
      {
        if (entity != enemy && epos == nextPos)
        {
          blocked = true;
          if (team.team != enemy_team.team)
            hp.hitpoints -= dmg.damage;
        }
      });
      if (blocked)
        a.action = EA_NOP;
      else
        mpos = nextPos;
    });
    // now move
    processActions.each([&](flecs::entity entity, Action &a, Position &pos, MovePos &mpos, const MeleeDamage &, const Team&)
    {
      pos = mpos;
      a.action = EA_NOP;
    });
  });

  static auto deleteAllDead = ecs.query<const Hitpoints>();
  ecs.defer([&]
  {
    deleteAllDead.each([&](flecs::entity entity, const Hitpoints &hp)
    {
      if (hp.hitpoints <= 0.f)
        entity.destruct();
    });
  });

  static auto playerPickup = ecs.query<const IsPlayer, const Position, Hitpoints, MeleeDamage>();
  static auto healPickup = ecs.query<const Position, const HealAmount>();
  static auto powerupPickup = ecs.query<const Position, const PowerupAmount>();
  ecs.defer([&]
  {
    playerPickup.each([&](const IsPlayer&, const Position &pos, Hitpoints &hp, MeleeDamage &dmg)
    {
      healPickup.each([&](flecs::entity entity, const Position &ppos, const HealAmount &amt)
      {
        if (pos == ppos)
        {
          hp.hitpoints += amt.amount;
          entity.destruct();
        }
      });
      powerupPickup.each([&](flecs::entity entity, const Position &ppos, const PowerupAmount &amt)
      {
        if (pos == ppos)
        {
          dmg.damage += amt.amount;
          entity.destruct();
        }
      });
    });
  });

  static auto countCooldown = ecs.query<Cooldown>();
  ecs.defer([&]
  {
    countCooldown.each([&](Cooldown &cooldown)
    {
      if (cooldown.time > 0) --cooldown.time;
    });
  });
}

void process_turn(flecs::world &ecs)
{
  static auto stateMachineAct = ecs.query<StateMachine>();
  if (is_player_acted(ecs))
  {
    if (upd_player_actions_count(ecs))
    {
      // Plan action for NPCs
      ecs.defer([&]
      {
        stateMachineAct.each([&](flecs::entity e, StateMachine &sm)
        {
          sm.act(0.f, ecs, e);
        });
      });
    }
    process_actions(ecs);
  }
}

void print_stats(flecs::world &ecs)
{
  static auto playerStatsQuery = ecs.query<const IsPlayer, const Hitpoints, const MeleeDamage>();
  playerStatsQuery.each([&](const IsPlayer &, const Hitpoints &hp, const MeleeDamage &dmg)
  {
    DrawText(TextFormat("hp: %d", int(hp.hitpoints)), 20, 20, 20, WHITE);
    DrawText(TextFormat("power: %d", int(dmg.damage)), 20, 40, 20, WHITE);
  });
}

