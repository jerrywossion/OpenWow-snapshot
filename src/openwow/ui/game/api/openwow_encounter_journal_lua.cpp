#include "openwow/ui/game/api/openwow_encounter_journal_lua.h"

#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openwow::ui::game {

namespace {

constexpr std::string_view kApiTableName = "C_OpenWoWJournal";
constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::uint32_t kRandomDungeonType = 6;

struct InstanceRecord final {
  const openwow::data::dbc::LfgDungeonsEntry* lfg{nullptr};
  const openwow::data::dbc::MapEntry* map{nullptr};
  bool is_raid{false};
};

const openwow::data::dbc::DbcLoader* GetDbc() {
  const auto* dbc = openwow::data::GetBoundDbcTableRegistryLoader();
  if (dbc == nullptr) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "Encounter journal query failed: build 12340 DBC registry is not bound");
  }
  return dbc;
}

bool IsSupportedMapType(const openwow::data::dbc::MapEntry& map,
                        const bool is_raid) {
  const auto expected =
      is_raid ? openwow::data::dbc::MapType::kRaid
              : openwow::data::dbc::MapType::kInstance;
  return map.map_type == static_cast<std::uint32_t>(expected);
}

bool IsBetterRepresentative(
    const openwow::data::dbc::LfgDungeonsEntry& candidate,
    const openwow::data::dbc::LfgDungeonsEntry& current,
    const bool is_raid) {
  const auto preferred_type = is_raid ? 2u : 1u;
  const auto candidate_key = std::tuple{
      candidate.type_id == preferred_type ? 0u : 1u,
      candidate.difficulty == 0 ? 0u : 1u,
      candidate.order_index,
      candidate.id,
  };
  const auto current_key = std::tuple{
      current.type_id == preferred_type ? 0u : 1u,
      current.difficulty == 0 ? 0u : 1u,
      current.order_index,
      current.id,
  };
  return candidate_key < current_key;
}

std::vector<InstanceRecord> BuildInstances(
    const openwow::data::dbc::DbcLoader& dbc,
    const bool is_raid) {
  std::unordered_set<std::uint32_t> maps_with_encounters;
  maps_with_encounters.reserve(dbc.dungeon_encounter().size());
  for (const auto& encounter : dbc.dungeon_encounter()) {
    maps_with_encounters.insert(encounter.map_id);
  }

  std::unordered_map<std::uint32_t, InstanceRecord> by_map;
  by_map.reserve(dbc.lfg_dungeons().size());
  for (const auto& lfg : dbc.lfg_dungeons()) {
    if (lfg.type_id == kRandomDungeonType ||
        !maps_with_encounters.contains(lfg.map_id)) {
      continue;
    }

    const auto* map = dbc.map().LookupEntry(lfg.map_id);
    if (map == nullptr || !IsSupportedMapType(*map, is_raid)) {
      continue;
    }

    auto [it, inserted] = by_map.try_emplace(
        lfg.map_id, InstanceRecord{.lfg = &lfg, .map = map, .is_raid = is_raid});
    if (!inserted && IsBetterRepresentative(lfg, *it->second.lfg, is_raid)) {
      it->second.lfg = &lfg;
    }
  }

  std::vector<InstanceRecord> instances;
  instances.reserve(by_map.size());
  for (const auto& [map_id, record] : by_map) {
    (void)map_id;
    instances.push_back(record);
  }
  std::sort(instances.begin(), instances.end(),
            [](const InstanceRecord& left, const InstanceRecord& right) {
              const auto left_key = std::tuple{
                  left.lfg->expansion_level,
                  left.lfg->order_index,
                  left.lfg->min_level,
                  left.map->id,
              };
              const auto right_key = std::tuple{
                  right.lfg->expansion_level,
                  right.lfg->order_index,
                  right.lfg->min_level,
                  right.map->id,
              };
              return left_key < right_key;
            });
  return instances;
}

std::vector<std::uint32_t> BuildDifficulties(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t map_id) {
  std::vector<std::uint32_t> difficulties;
  for (const auto& encounter : dbc.dungeon_encounter()) {
    if (encounter.map_id == map_id &&
        std::find(difficulties.begin(), difficulties.end(),
                  encounter.difficulty) == difficulties.end()) {
      difficulties.push_back(encounter.difficulty);
    }
  }
  std::sort(difficulties.begin(), difficulties.end());
  return difficulties;
}

std::vector<const openwow::data::dbc::DungeonEncounterEntry*> BuildEncounters(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t map_id,
    const std::uint32_t difficulty) {
  std::vector<const openwow::data::dbc::DungeonEncounterEntry*> encounters;
  for (const auto& encounter : dbc.dungeon_encounter()) {
    if (encounter.map_id == map_id && encounter.difficulty == difficulty) {
      encounters.push_back(&encounter);
    }
  }
  std::stable_sort(
      encounters.begin(), encounters.end(),
      [](const auto* left, const auto* right) {
        return left->order_index < right->order_index;
      });
  return encounters;
}

void PushString(lua_State* state, const std::string_view value) {
  lua_pushlstring(state, value.data(), value.size());
}

void PushIconPath(lua_State* state,
                  const openwow::data::dbc::DbcLoader& dbc,
                  const std::uint32_t spell_icon_id) {
  const auto* icon = dbc.spell_icon().LookupEntry(spell_icon_id);
  if (icon == nullptr || icon->icon_path.empty()) {
    lua_pushnil(state);
    return;
  }
  PushString(state, icon->icon_path);
}

std::uint32_t CheckUnsigned(lua_State* state,
                           const int argument,
                           const char* label) {
  const auto value = luaL_checkinteger(state, argument);
  if (value < 0 ||
      static_cast<unsigned long long>(value) >
          std::numeric_limits<std::uint32_t>::max()) {
    luaL_error(state, "%s must be an unsigned 32-bit integer", label);
  }
  return static_cast<std::uint32_t>(value);
}

std::size_t CheckOneBasedIndex(lua_State* state, const int argument) {
  const auto index = luaL_checkinteger(state, argument);
  if (index <= 0) {
    luaL_error(state, "index must be one-based");
  }
  return static_cast<std::size_t>(index - 1);
}

int LuaGetSchemaVersion(lua_State* state) {
  lua_pushinteger(state, kSchemaVersion);
  return 1;
}

int LuaGetNumInstances(lua_State* state) {
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto instances = BuildInstances(*dbc, lua_toboolean(state, 1) != 0);
  lua_pushinteger(state, static_cast<lua_Integer>(instances.size()));
  return 1;
}

int LuaGetInstanceByIndex(lua_State* state) {
  const auto index = CheckOneBasedIndex(state, 1);
  const bool is_raid = lua_toboolean(state, 2) != 0;
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto instances = BuildInstances(*dbc, is_raid);
  if (index >= instances.size()) {
    lua_pushnil(state);
    return 1;
  }

  const auto& instance = instances[index];
  lua_pushinteger(state, instance.map->id);
  PushString(state, !instance.lfg->name.empty() ? instance.lfg->name
                                                : instance.map->name);
  PushString(state, instance.lfg->description);
  PushString(state, instance.lfg->texture_filename);
  lua_pushinteger(state, instance.lfg->min_level);
  lua_pushinteger(state, instance.lfg->max_level);
  lua_pushinteger(state, instance.lfg->expansion_level);
  lua_pushboolean(state, instance.is_raid ? 1 : 0);
  lua_pushinteger(state, instance.lfg->id);
  return 9;
}

int LuaGetNumDifficulties(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto difficulties = BuildDifficulties(*dbc, map_id);
  lua_pushinteger(state, static_cast<lua_Integer>(difficulties.size()));
  return 1;
}

int LuaGetDifficultyByIndex(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto index = CheckOneBasedIndex(state, 2);
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto difficulties = BuildDifficulties(*dbc, map_id);
  if (index >= difficulties.size()) {
    lua_pushnil(state);
    return 1;
  }

  const auto difficulty = difficulties[index];
  const auto* map_difficulty =
      openwow::data::DBClient_FindMapDifficulty(dbc, map_id, difficulty);
  lua_pushinteger(state, difficulty);
  if (map_difficulty == nullptr) {
    lua_pushnil(state);
    lua_pushnil(state);
    return 3;
  }
  lua_pushinteger(state, map_difficulty->max_players);
  PushString(state, map_difficulty->difficulty_string);
  return 3;
}

int LuaGetNumEncounters(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto difficulty = CheckUnsigned(state, 2, "difficulty");
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushinteger(state, 0);
    return 1;
  }
  const auto encounters = BuildEncounters(*dbc, map_id, difficulty);
  lua_pushinteger(state, static_cast<lua_Integer>(encounters.size()));
  return 1;
}

int LuaGetEncounterByIndex(lua_State* state) {
  const auto map_id = CheckUnsigned(state, 1, "mapID");
  const auto difficulty = CheckUnsigned(state, 2, "difficulty");
  const auto index = CheckOneBasedIndex(state, 3);
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto encounters = BuildEncounters(*dbc, map_id, difficulty);
  if (index >= encounters.size()) {
    lua_pushnil(state);
    return 1;
  }

  const auto& encounter = *encounters[index];
  lua_pushinteger(state, encounter.id);
  PushString(state, encounter.name);
  PushIconPath(state, *dbc, encounter.spell_icon_id);
  lua_pushinteger(state, encounter.order_index);
  return 4;
}

int LuaGetSourceSummary(lua_State* state) {
  const auto* dbc = GetDbc();
  if (dbc == nullptr) {
    lua_pushnil(state);
    return 1;
  }

  const auto dungeons = BuildInstances(*dbc, false);
  const auto raids = BuildInstances(*dbc, true);
  lua_pushinteger(state, static_cast<lua_Integer>(dungeons.size()));
  lua_pushinteger(state, static_cast<lua_Integer>(raids.size()));
  lua_pushinteger(state,
                  static_cast<lua_Integer>(dbc->dungeon_encounter().size()));
  return 3;
}

void SetFunction(lua_State* state,
                 const char* name,
                 const lua_CFunction function) {
  lua_pushcfunction(state, function);
  lua_setfield(state, -2, name);
}

void InstallEncounterJournal(lua_State* state, void*) {
  lua_newtable(state);
  SetFunction(state, "GetSchemaVersion", LuaGetSchemaVersion);
  SetFunction(state, "GetNumInstances", LuaGetNumInstances);
  SetFunction(state, "GetInstanceByIndex", LuaGetInstanceByIndex);
  SetFunction(state, "GetNumDifficulties", LuaGetNumDifficulties);
  SetFunction(state, "GetDifficultyByIndex", LuaGetDifficultyByIndex);
  SetFunction(state, "GetNumEncounters", LuaGetNumEncounters);
  SetFunction(state, "GetEncounterByIndex", LuaGetEncounterByIndex);
  SetFunction(state, "GetSourceSummary", LuaGetSourceSummary);
  lua_setglobal(state, kApiTableName.data());
}

void UninstallEncounterJournal(lua_State* state, void*) {
  lua_pushnil(state);
  lua_setglobal(state, kApiTableName.data());
}

}

lua::NativeBindingCatalog EncounterJournalNativeBindingCatalog() {
  return {
      .owner = "openwow.extensions.encounter_journal",
      .scope = lua::BindingScope::kWorld,
      .install = InstallEncounterJournal,
      .uninstall = UninstallEncounterJournal,
  };
}

}
