/*
    Copyright (c) 1996-2020 Freeciv21 and Freeciv  contributors. This file
                         is part of Freeciv21. Freeciv21 is free software:
|\_/|,,_____,~~`        you can redistribute it and/or modify it under the
(.".)~~     )`~}}    terms of the GNU General Public License  as published
 \o/\ /---~\\ ~}}     by the Free Software Foundation, either version 3 of
   _//    _// ~}       the License, or (at your option) any later version.
                        You should have received a copy of the GNU General
                          Public License along with Freeciv21. If not, see
                                            https://www.gnu.org/licenses/.
 */

#include <QSet>
#include <climits> // USHRT_MAX

// utility
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "shared.h"
#include "support.h"

// common
#include "events.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "research.h"
#include "terrain.h"
#include "unitlist.h"

// server
#include "aiiface.h"
#include "citytools.h"
#include "cityturn.h"
#include "connecthand.h"
#include "gamehand.h"
#include "hand_gen.h"
#include "maphand.h"
#include "notify.h"
#include "plrhand.h"
#include "sanitycheck.h"
#include "techtools.h"
#include "unittools.h"

/* server/generator */
#include "mapgen_utils.h"

/* server/savegame */
#include "savemain.h"

#include "edithand.h"

/* Set if anything in a sequence of edits triggers the expensive
 * assign_continent_numbers() check, which will be done once when the
 * sequence is complete. */
static bool need_continents_reassigned = false;
/* Hold pointers to tiles which were changed during the edit sequence,
 * so that they can be sanity-checked when the sequence is complete
 * and final global fix-ups have been done. */
Q_GLOBAL_STATIC(QSet<const struct tile *>, modified_tile_table)
/* Array of size player_slot_count() indexed by player
 * number to tell whether a given player has fog of war
 * disabled in edit mode. */
static bool *unfogged_players;

/**
   Initialize data structures required for edit mode.
 */
void edithand_init()
{
  modified_tile_table->clear();

  need_continents_reassigned = false;

  delete[] unfogged_players;
  unfogged_players = new bool[MAX_NUM_PLAYER_SLOTS]();
}

/**
   Free all memory used by data structures required for edit mode.
 */
void edithand_free()
{
  delete[] unfogged_players;
  unfogged_players = nullptr;
}

/**
   Send the needed packets for connections entering in the editing mode.
 */
void edithand_send_initial_packets(struct conn_list *dest)
{
  struct packet_edit_startpos startpos;
  struct packet_edit_startpos_full startpos_full;

  if (nullptr == dest) {
    dest = game.est_connections;
  }

  // Send map start positions.
  for (auto *psp : std::as_const(*wld.map.startpos_table)) {
    if (psp->exclude) {
      continue;
    }
    startpos.id = tile_index(startpos_tile(psp));
    startpos.removal = false;
    startpos.tag = 0;

    startpos_pack(psp, &startpos_full);

    conn_list_iterate(dest, pconn)
    {
      if (can_conn_edit(pconn)) {
        send_packet_edit_startpos(pconn, &startpos);
        send_packet_edit_startpos_full(pconn, &startpos_full);
      }
    }
    conn_list_iterate_end;
  }
}

/**
   Do the potentially slow checks required after one or several tiles'
   terrain has change.
 */
static void check_edited_tile_terrains()
{
  if (need_continents_reassigned) {
    assign_continent_numbers();
    send_all_known_tiles(nullptr);
    need_continents_reassigned = false;
  }

#ifdef SANITY_CHECKING
  for (const auto *ptile : *modified_tile_table) {
    sanity_check_tile(const_cast<struct tile *>(ptile));
  }
#endif // SANITY_CHECKING
  modified_tile_table->clear();
}

/**
   Do any necessary checks after leaving edit mode to ensure that the game
   is in a consistent state.
 */
static void check_leaving_edit_mode()
{
  bool unfogged;

  conn_list_do_buffer(game.est_connections);
  players_iterate(pplayer)
  {
    unfogged = unfogged_players[player_number(pplayer)];
    if (unfogged && game.info.fogofwar) {
      enable_fog_of_war_player(pplayer);
    } else if (!unfogged && !game.info.fogofwar) {
      disable_fog_of_war_player(pplayer);
    }
  }
  players_iterate_end;

  // Clear the whole array.
  memset(unfogged_players, 0, MAX_NUM_PLAYER_SLOTS * sizeof(bool));

  check_edited_tile_terrains();
  conn_list_do_unbuffer(game.est_connections);
}

/**
   Handles a request by the client to enter edit mode.
 */
void handle_edit_mode(struct connection *pc, bool is_edit_mode)
{
  if (!can_conn_enable_editing(pc)) {
    return;
  }

  if (!game.info.is_edit_mode && is_edit_mode) {
    // Someone could be cheating! Warn people.
    notify_conn(nullptr, nullptr, E_SETTING, ftc_editor,
                _(" *** Server set to edit mode by %s! *** "),
                conn_description(pc, false));
  }

  if (game.info.is_edit_mode && !is_edit_mode) {
    notify_conn(nullptr, nullptr, E_SETTING, ftc_editor,
                _(" *** Edit mode canceled by %s. *** "),
                conn_description(pc, false));

    check_leaving_edit_mode();
  }

  if (game.info.is_edit_mode != is_edit_mode) {
    game.info.is_edit_mode = is_edit_mode;

    send_game_info(nullptr);
    edithand_send_initial_packets(nullptr);
  }
}

/**
   Base function to edit the terrain property of a tile. Returns TRUE if
   the terrain has changed.
 */
static bool edit_tile_terrain_handling(struct tile *ptile,
                                       struct terrain *pterrain,
                                       bool send_info)
{
  struct terrain *old_terrain = tile_terrain(ptile);

  if (old_terrain == pterrain
      || (terrain_has_flag(pterrain, TER_NO_CITIES)
          && nullptr != tile_city(ptile))) {
    return false;
  }

  tile_change_terrain(ptile, pterrain);
  fix_tile_on_terrain_change(ptile, old_terrain, false);
  modified_tile_table->insert(ptile);
  if (need_to_reassign_continents(old_terrain, pterrain)) {
    need_continents_reassigned = true;
  }

  if (send_info) {
    update_tile_knowledge(ptile);
  }

  return true;
}

/**
   Base function to edit the extras property of a tile. Returns TRUE if
   the extra state has changed.
 */
static bool edit_tile_extra_handling(struct tile *ptile,
                                     struct extra_type *pextra,
                                     bool remove_mode, bool send_info)
{
  if (remove_mode) {
    if (!tile_has_extra(ptile, pextra)) {
      return false;
    }

    if (!tile_extra_rm_apply(ptile, pextra)) {
      return false;
    }

    terrain_changed(ptile);

  } else {
    if (tile_has_extra(ptile, pextra)) {
      return false;
    }

    if (!tile_extra_apply(ptile, pextra)) {
      return false;
    }
  }

  if (send_info) {
    update_tile_knowledge(ptile);
  }

  return true;
}

/**
   Handles a client request to change the terrain of the tile at the given
   x, y coordinates. The 'size' parameter indicates that all tiles in a
   square of "radius" 'size' should be affected. So size=1 corresponds to
   the single tile case.
 */
void handle_edit_tile_terrain(struct connection *pc, int tile,
                              Terrain_type_id terrain, int size)
{
  struct terrain *pterrain;
  struct tile *ptile_center;

  ptile_center = index_to_tile(&(wld.map), tile);
  if (!ptile_center) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot edit the tile because %d is not a valid "
                  "tile index on this map!"),
                tile);
    return;
  }

  pterrain = terrain_by_number(terrain);
  if (!pterrain) {
    notify_conn(pc->self, ptile_center, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." the tile <tile-coordinates> because"...
                _("Cannot modify terrain for the tile %s because "
                  "%d is not a valid terrain id."),
                tile_link(ptile_center), terrain);
    return;
  }

  conn_list_do_buffer(game.est_connections);
  /* This iterates outward, which gives any units that can't survive on
   * changed terrain the best chance of survival. */
  square_iterate(&(wld.map), ptile_center, size - 1, ptile)
  {
    edit_tile_terrain_handling(ptile, pterrain, true);
  }
  square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/**
   Handle a request to change one or more tiles' extras. The 'remove'
   argument controls whether to remove or add the given extra from the tile.
 */
void handle_edit_tile_extra(struct connection *pc, int tile, int id,
                            bool removal, int eowner, int size)
{
  struct tile *ptile_center;
  struct player *plr_eowner;

  ptile_center = index_to_tile(&(wld.map), tile);
  if (!ptile_center) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot edit the tile because %d is not a valid "
                  "tile index on this map!"),
                tile);
    return;
  }

  if (id < 0 || id >= game.control.num_extra_types) {
    notify_conn(pc->self, ptile_center, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." the tile <tile-coordinates> because"...
                _("Cannot modify extras for the tile %s because "
                  "%d is not a valid extra id."),
                tile_link(ptile_center), id);
    return;
  }

  if (eowner != MAP_TILE_OWNER_NULL) {
    plr_eowner = player_by_number(eowner);
  } else {
    plr_eowner = nullptr;
  }

  conn_list_do_buffer(game.est_connections);
  square_iterate(&(wld.map), ptile_center, size - 1, ptile)
  {
    ptile->extras_owner = plr_eowner;
    edit_tile_extra_handling(ptile, extra_by_number(id), removal, true);
  }
  square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/**
   Handles tile information from the client, to make edits to tiles.
 */
void handle_edit_tile(struct connection *pc,
                      const struct packet_edit_tile *packet)
{
  struct tile *ptile;
  struct player *eowner;
  bool changed = false;

  ptile = index_to_tile(&(wld.map), packet->tile);
  if (!ptile) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot edit the tile because %d is not a valid "
                  "tile index on this map!"),
                packet->tile);
    return;
  }

  if (packet->eowner != MAP_TILE_OWNER_NULL) {
    eowner = player_by_number(packet->eowner);
  } else {
    eowner = nullptr;
  }

  // Handle changes in extras.
  if (!BV_ARE_EQUAL(packet->extras, ptile->extras)) {
    extra_type_iterate(pextra)
    {
      if (edit_tile_extra_handling(
              ptile, pextra, !BV_ISSET(packet->extras, extra_number(pextra)),
              false)) {
        changed = true;
      }
    }
    extra_type_iterate_end;
  }

  if (ptile->extras_owner != eowner) {
    ptile->extras_owner = eowner;
    changed = true;
  }

  // Handle changes in label
  if (tile_set_label(ptile, packet->label)) {
    changed = true;
  }

  // TODO: Handle more property edits.

  // Send the new state to all affected.
  if (changed) {
    update_tile_knowledge(ptile);
    send_tile_info(nullptr, ptile, false);
  }
}

/**
   Handle a request to create 'count' units of type 'utid' at the tile given
   by the x, y coordinates and owned by player with number 'owner'.
 */
void handle_edit_unit_create(struct connection *pc, int owner, int tile,
                             Unit_type_id utid, int count, int tag)
{
  struct tile *ptile;
  struct unit_type *punittype;
  struct player *pplayer;
  struct city *homecity;
  struct unit *punit;
  int id, i;

  ptile = index_to_tile(&(wld.map), tile);
  if (!ptile) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot create units because %d is not a valid "
                  "tile index on this map!"),
                tile);
    return;
  }

  punittype = utype_by_number(utid);
  if (!punittype) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." at <tile-coordinates> because"...
                _("Cannot create a unit at %s because the "
                  "given unit type id %d is invalid."),
                tile_link(ptile), utid);
    return;
  }

  pplayer = player_by_number(owner);
  if (!pplayer) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." type <unit-type> at <tile-coordinates>"...
                _("Cannot create a unit of type %s at %s "
                  "because the given owner's player id %d is "
                  "invalid."),
                utype_name_translation(punittype), tile_link(ptile), owner);
    return;
  }

  if (is_non_allied_unit_tile(ptile, pplayer)
      || (tile_city(ptile)
          && !pplayers_allied(pplayer, city_owner(tile_city(ptile))))) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                /* TRANS: ..." type <unit-type> on enemy tile
                 * <tile-coordinates>"... */
                _("Cannot create unit of type %s on enemy tile "
                  "%s."),
                utype_name_translation(punittype), tile_link(ptile));
    return;
  }

  if (!can_exist_at_tile(&(wld.map), punittype, ptile)) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                /* TRANS: ..." type <unit-type> on the terrain at
                 * <tile-coordinates>"... */
                _("Cannot create a unit of type %s on the terrain "
                  "at %s."),
                utype_name_translation(punittype), tile_link(ptile));
    return;
  }

  if (count > 0 && !pplayer->is_alive) {
    pplayer->is_alive = true;
    send_player_info_c(pplayer, nullptr);
  }

  homecity = find_closest_city(ptile, nullptr, pplayer, false, false, false,
                               true, false, utype_class(punittype));
  id = homecity ? homecity->id : 0;

  conn_list_do_buffer(game.est_connections);
  map_show_circle(pplayer, ptile, punittype->vision_radius_sq);
  for (i = 0; i < count; i++) {
    /* As far as I can see create_unit is guaranteed to
     * never return nullptr. */
    punit = create_unit(pplayer, ptile, punittype, 0, id, -1);
    if (tag > 0) {
      dsend_packet_edit_object_created(pc, tag, punit->id);
    }
  }
  conn_list_do_unbuffer(game.est_connections);
}

/**
   Remove 'count' units of type 'utid' owned by player number 'owner' at
   tile (x, y).
 */
void handle_edit_unit_remove(struct connection *pc, int owner, int tile,
                             Unit_type_id utid, int count)
{
  struct tile *ptile;
  struct unit_type *punittype;
  struct player *pplayer;
  int i;

  ptile = index_to_tile(&(wld.map), tile);
  if (!ptile) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot remove units because %d is not a valid "
                  "tile index on this map!"),
                tile);
    return;
  }

  punittype = utype_by_number(utid);
  if (!punittype) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." at <tile-coordinates> because"...
                _("Cannot remove a unit at %s because the "
                  "given unit type id %d is invalid."),
                tile_link(ptile), utid);
    return;
  }

  pplayer = player_by_number(owner);
  if (!pplayer) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                /* TRANS: ..." type <unit-type> at <tile-coordinates>
                 * because"... */
                _("Cannot remove a unit of type %s at %s "
                  "because the given owner's player id %d is "
                  "invalid."),
                utype_name_translation(punittype), tile_link(ptile), owner);
    return;
  }

  i = 0;
  unit_list_iterate_safe(ptile->units, punit)
  {
    if (i >= count) {
      break;
    }
    if (unit_type_get(punit) != punittype || unit_owner(punit) != pplayer) {
      continue;
    }
    wipe_unit(punit, ULR_EDITOR, nullptr);
    i++;
  }
  unit_list_iterate_safe_end;
}

/**
   Handle a request to remove a unit given by its id.
 */
void handle_edit_unit_remove_by_id(struct connection *pc, Unit_type_id id)
{
  struct unit *punit;

  punit = game_unit_by_number(id);
  if (!punit) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("No such unit (ID %d)."), id);
    return;
  }

  wipe_unit(punit, ULR_EDITOR, nullptr);
}

/**
   Handles unit information from the client, to make edits to units.
 */
void handle_edit_unit(struct connection *pc,
                      const struct packet_edit_unit *packet)
{
  const struct unit_type *putype;
  struct unit *punit;
  int id;
  bool changed = false;
  int fuel, hp;

  id = packet->id;
  punit = game_unit_by_number(id);
  if (!punit) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("No such unit (ID %d)."), id);
    return;
  }

  putype = unit_type_get(punit);

  if (packet->moves_left != punit->moves_left) {
    punit->moves_left = packet->moves_left;
    changed = true;
  }

  fuel = CLIP(0, packet->fuel, utype_fuel(putype));
  if (fuel != punit->fuel) {
    punit->fuel = fuel;
    changed = true;
  }

  if (packet->moved != punit->moved) {
    punit->moved = packet->moved;
    changed = true;
  }

  if (packet->done_moving != punit->done_moving) {
    punit->done_moving = packet->done_moving;
    changed = true;
  }

  hp = CLIP(1, packet->hp, putype->hp);
  if (hp != punit->hp) {
    punit->hp = hp;
    changed = true;
  }

  if (packet->veteran != punit->veteran) {
    int v = packet->veteran;
    if (!utype_veteran_level(putype, v)) {
      notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                  _("Invalid veteran level %d for unit %d (%s)."), v, id,
                  unit_link(punit));
    } else {
      punit->veteran = v;
      changed = true;
    }
  }

  if (packet->stay != punit->stay) {
    punit->stay = packet->stay;
    changed = true;
  }

  // TODO: Handle more property edits.

  // Send the new state to all affected.
  if (changed) {
    send_unit_info(nullptr, punit);
  }
}

/**
   Allows the editing client to create a city at the given position and
   of size 'size'.
 */
void handle_edit_city_create(struct connection *pc, int owner, int tile,
                             int size, int tag)
{
  struct tile *ptile;
  struct city *pcity;
  struct player *pplayer;

  ptile = index_to_tile(&(wld.map), tile);
  if (!ptile) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot create a city because %d is not a valid "
                  "tile index on this map!"),
                tile);
    return;
  }

  pplayer = player_by_number(owner);
  if (!pplayer) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." at <tile-coordinates> because"...
                _("Cannot create a city at %s because the "
                  "given owner's player id %d is invalid"),
                tile_link(ptile), owner);
    return;
  }

  if (is_enemy_unit_tile(ptile, pplayer) != nullptr
      || !city_can_be_built_here(ptile, nullptr)) {
    notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." at <tile-coordinates>."
                _("A city may not be built at %s."), tile_link(ptile));
    return;
  }

  if (!pplayer->is_alive) {
    pplayer->is_alive = true;
    send_player_info_c(pplayer, nullptr);
  }

  conn_list_do_buffer(game.est_connections);

  map_show_tile(pplayer, ptile);
  create_city(pplayer, ptile, city_name_suggestion(pplayer, ptile), pplayer);
  pcity = tile_city(ptile);

  if (size > 1) {
    // FIXME: Slow and inefficient for large size changes.
    city_change_size(pcity, CLIP(1, size, MAX_CITY_SIZE), pplayer, nullptr);
    send_city_info(nullptr, pcity);
  }

  if (tag > 0) {
    dsend_packet_edit_object_created(pc, tag, pcity->id);
  }

  conn_list_do_unbuffer(game.est_connections);
}

/**
   Handle a request to change the internal state of a city.
 */
void handle_edit_city(struct connection *pc,
                      const struct packet_edit_city *packet)
{
  struct tile *ptile;
  struct city *pcity, *oldcity;
  struct player *pplayer;
  char buf[1024];
  int id;
  bool changed = false;
  bool need_game_info = false;
  bv_player need_player_info;

  pcity = game_city_by_number(packet->id);
  if (!pcity) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot edit city with invalid city ID %d."), packet->id);
    return;
  }

  pplayer = city_owner(pcity);
  ptile = city_tile(pcity);
  BV_CLR_ALL(need_player_info);

  // Handle name change.
  if (0 != strcmp(pcity->name, packet->name)) {
    if (!is_allowed_city_name(pplayer, packet->name, buf, sizeof(buf))) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                  _("Cannot edit city name: %s"), buf);
    } else {
      sz_strlcpy(pcity->name, packet->name);
      changed = true;
    }
  }

  // Handle size change.
  if (packet->size != city_size_get(pcity)) {
    if (!(0 < packet->size && packet->size <= MAX_CITY_SIZE)) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                  _("Invalid city size %d for city %s."), packet->size,
                  city_link(pcity));
    } else {
      // FIXME: Slow and inefficient for large size changes.
      city_change_size(pcity, packet->size, nullptr, nullptr);
      changed = true;
    }
  }

  if (packet->history != pcity->history) {
    pcity->history = packet->history;
    changed = true;
  }

  // Handle city improvement changes.
  improvement_iterate(pimprove)
  {
    oldcity = nullptr;
    id = improvement_number(pimprove);

    if (is_special_improvement(pimprove)) {
      if (packet->built[id] >= 0) {
        notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                    _("It is impossible for a city to have %s!"),
                    improvement_name_translation(pimprove));
      }
      continue;
    }

    /* FIXME: game.info.great_wonder_owners and pplayer->wonders
     * logic duplication with city_build_building. */

    if (city_has_building(pcity, pimprove) && packet->built[id] < 0) {
      city_remove_improvement(pcity, pimprove);
      changed = true;

    } else if (!city_has_building(pcity, pimprove)
               && packet->built[id] >= 0) {
      if (is_great_wonder(pimprove)) {
        oldcity = city_from_great_wonder(pimprove);
        if (oldcity != pcity) {
          BV_SET(need_player_info, player_index(pplayer));
        }
        if (nullptr != oldcity && city_owner(oldcity) != pplayer) {
          // Great wonders make more changes.
          need_game_info = true;
          BV_SET(need_player_info, player_index(city_owner(oldcity)));
        }
      } else if (is_small_wonder(pimprove)) {
        oldcity = city_from_small_wonder(pplayer, pimprove);
        if (oldcity != pcity) {
          BV_SET(need_player_info, player_index(pplayer));
        }
      }

      if (oldcity) {
        city_remove_improvement(oldcity, pimprove);
        city_refresh_queue_add(oldcity);
      }

      city_add_improvement(pcity, pimprove);
      changed = true;
    }
  }
  improvement_iterate_end;

  // Handle food stock change.
  if (packet->food_stock != pcity->food_stock) {
    int max = city_granary_size(city_size_get(pcity));
    if (!(0 <= packet->food_stock && packet->food_stock <= max)) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                  _("Invalid city food stock amount %d for city %s "
                    "(allowed range is %d to %d)."),
                  packet->food_stock, city_link(pcity), 0, max);
    } else {
      pcity->food_stock = packet->food_stock;
      changed = true;
    }
  }

  // Handle shield stock change.
  if (packet->shield_stock != pcity->shield_stock) {
    int max = USHRT_MAX; // Limited to uint16 by city info packet.
    if (!(0 <= packet->shield_stock && packet->shield_stock <= max)) {
      notify_conn(pc->self, ptile, E_BAD_COMMAND, ftc_editor,
                  _("Invalid city shield stock amount %d for city %s "
                    "(allowed range is %d to %d)."),
                  packet->shield_stock, city_link(pcity), 0, max);
    } else {
      pcity->shield_stock = packet->shield_stock;
      changed = true;
    }
  }

  // TODO: Handle more property edits.

  if (changed) {
    city_refresh_queue_add(pcity);
    conn_list_do_buffer(game.est_connections);
    city_refresh_queue_processing();

    // FIXME: city_refresh_queue_processing only sends to city owner?
    send_city_info(nullptr, pcity);

    conn_list_do_unbuffer(game.est_connections);
  }

  // Update wonder infos.
  if (need_game_info) {
    send_game_info(nullptr);
  }
  if (BV_ISSET_ANY(need_player_info)) {
    players_iterate(aplayer)
    {
      if (BV_ISSET(need_player_info, player_index(aplayer))) {
        // No need to send to detached connections.
        send_player_info_c(aplayer, nullptr);
      }
    }
    players_iterate_end;
  }
}

/**
   Handle a request to create a new player.
 */
void handle_edit_player_create(struct connection *pc, int tag)
{
  struct player *pplayer;
  struct nation_type *pnation;
  struct research *presearch;

  if (player_count() >= MAX_NUM_PLAYER_SLOTS) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("No more players can be added because the maximum "
                  "number of players (%d) has been reached."),
                MAX_NUM_PLAYER_SLOTS);
    return;
  }

  if (player_count() >= game.control.nation_count) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("No more players can be added because there are "
                  "no available nations (%d used)."),
                game.control.nation_count);
    return;
  }

  pnation = pick_a_nation(nullptr, true, true, NOT_A_BARBARIAN);
  if (!pnation) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Player cannot be created because random nation "
                  "selection failed."));
    return;
  }

  pplayer = server_create_player(-1, default_ai_type_name(), nullptr, false);
  if (!pplayer) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Player creation failed."));
    return;
  }
  server_player_init(pplayer, true, true);

  player_nation_defaults(pplayer, pnation, true);
  if (game_was_started()) {
    // Find a color for the new player.
    assign_player_colors();
  }
  sz_strlcpy(pplayer->username, _(ANON_USER_NAME));
  pplayer->unassigned_user = true;
  pplayer->is_connected = false;
  pplayer->government = init_government_of_nation(pnation);
  pplayer->server.got_first_city = false;

  pplayer->economic.gold = 0;
  pplayer->economic = player_limit_to_max_rates(pplayer);

  presearch = research_get(pplayer);
  init_tech(presearch, true);
  give_initial_techs(presearch, 0);

  send_player_all_c(pplayer, nullptr);
  /* Send research info after player info, else the client will complain
   * about invalid team. */
  send_research_info(presearch, nullptr);
  if (tag > 0) {
    dsend_packet_edit_object_created(pc, tag, player_number(pplayer));
  }
}

/**
   Handle a request to remove a player.
 */
void handle_edit_player_remove(struct connection *pc, int id)
{
  struct player *pplayer;

  pplayer = player_by_number(id);
  if (pplayer == nullptr) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("No such player (ID %d)."), id);
    return;
  }

  /* Don't use conn_list_iterate here because connection_detach() can be
   * recursive and free the next connection pointer. */
  while (conn_list_size(pplayer->connections) > 0) {
    connection_detach(conn_list_get(pplayer->connections, 0), false);
  }

  kill_player(pplayer);
  server_remove_player(pplayer);
}

/**
   Handle editing of any or all player properties.
 */
void handle_edit_player(struct connection *pc,
                        const struct packet_edit_player *packet)
{
  struct player *pplayer;
  bool changed = false, update_research = false;
  struct nation_type *pnation;
  struct research *research;
  enum tech_state known;
  struct government *gov;

  pplayer = player_by_number(packet->id);
  if (!pplayer) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot edit player with invalid player ID %d."),
                packet->id);
    return;
  }

  research = research_get(pplayer);

  // Handle player name change.
  if (0 != strcmp(packet->name, player_name(pplayer))) {
    char error_buf[256];

    if (server_player_set_name_full(pc, pplayer, nullptr, packet->name,
                                    error_buf, sizeof(error_buf))) {
      changed = true;
    } else {
      notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                  _("Cannot change name of player (%d) '%s' to '%s': %s"),
                  player_number(pplayer), player_name(pplayer), packet->name,
                  error_buf);
    }
  }

  // Handle nation change.
  pnation = nation_by_number(packet->nation);
  if (nation_of_player(pplayer) != pnation) {
    if (pnation == nullptr) {
      notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                  _("Cannot change nation for player %d (%s) "
                    "because the given nation ID %d is invalid."),
                  player_number(pplayer), player_name(pplayer),
                  packet->nation);
    } else if (pnation->player != nullptr) {
      notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                  _("Cannot change nation for player %d (%s) "
                    "to nation %d (%s) because that nation is "
                    "already assigned to player %d (%s)."),
                  player_number(pplayer), player_name(pplayer),
                  packet->nation, nation_plural_translation(pnation),
                  player_number(pnation->player),
                  player_name(pnation->player));
    } else if (!nation_is_in_current_set(pnation)) {
      notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                  _("Cannot change nation for player %d (%s) "
                    "to nation %d (%s) because that nation is "
                    "not in the current nation set."),
                  player_number(pplayer), player_name(pplayer),
                  packet->nation, nation_plural_translation(pnation));
    } else if (pplayer->ai_common.barbarian_type
                   != nation_barbarian_type(pnation)
               || (!is_barbarian(pplayer) && !is_nation_playable(pnation))) {
      notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                  _("Cannot change nation for player %d (%s) "
                    "to nation %d (%s) because that nation is "
                    "unsuitable for this player."),
                  player_number(pplayer), player_name(pplayer),
                  packet->nation, nation_plural_translation(pnation));
    } else {
      changed = player_set_nation(pplayer, pnation);
    }
  }

  // Handle a change in research progress.
  if (packet->bulbs_researched != research->bulbs_researched) {
    research->bulbs_researched = packet->bulbs_researched;
    changed = true;
    update_research = true;
  }

  // Handle a change in known inventions.
  advance_index_iterate(A_FIRST, tech)
  {
    known = research_invention_state(research, tech);
    if ((packet->inventions[tech] && known == TECH_KNOWN)
        || (!packet->inventions[tech] && known != TECH_KNOWN)) {
      continue;
    }
    if (packet->inventions[tech]) {
      // FIXME: Side-effect modifies game.info.global_advances.
      research_invention_set(research, tech, TECH_KNOWN);
      research->techs_researched++;
    } else {
      research_invention_set(research, tech, TECH_UNKNOWN);
      research->techs_researched--;
    }
    changed = true;
    update_research = true;
  }
  advance_index_iterate_end;

  // Handle a change in the player's gold.
  if (packet->gold != pplayer->economic.gold) {
    if (!(0 <= packet->gold && packet->gold <= 1000000)) {
      notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                  _("Cannot set gold for player %d (%s) because "
                    "the value %d is outside the allowed range."),
                  player_number(pplayer), player_name(pplayer),
                  packet->gold);
    } else {
      pplayer->economic.gold = packet->gold;
      changed = true;
    }
  }

  // Handle player government change
  gov = government_by_number(packet->government);
  if (gov != pplayer->government) {
    if (gov != game.government_during_revolution) {
      government_change(pplayer, gov, false);
    } else {
      int turns = revolution_length(gov, pplayer);

      if (turns >= 0) {
        pplayer->government = gov;
        pplayer->revolution_finishes = game.info.turn + turns;
      }
    }

    changed = true;
  }

  if (packet->scenario_reserved) {
    if (!player_has_flag(pplayer, PLRF_SCENARIO_RESERVED)) {
      changed = true;
      BV_SET(pplayer->flags, PLRF_SCENARIO_RESERVED);
    }
  } else {
    if (player_has_flag(pplayer, PLRF_SCENARIO_RESERVED)) {
      changed = true;
      BV_CLR(pplayer->flags, PLRF_SCENARIO_RESERVED);
    }
  }

  // TODO: Handle more property edits.

  if (update_research) {
    Tech_type_id current, goal;

    research_update(research);

    // FIXME: Modifies struct research directly.

    current = research->researching;
    goal = research->tech_goal;

    if (current != A_UNSET) {
      if (current != A_FUTURE) {
        known = research_invention_state(research, current);
        if (known != TECH_PREREQS_KNOWN) {
          research->researching = A_UNSET;
        }
      } else {
        // Future Tech is legal only if all techs are known
        advance_index_iterate(A_FIRST, tech_i)
        {
          known = research_invention_state(research, tech_i);
          if (known != TECH_KNOWN) {
            research->researching = A_UNSET;
            break;
          }
        }
        advance_index_iterate_end;
      }
    }
    if (goal != A_UNSET) {
      if (goal != A_FUTURE) {
        known = research_invention_state(research, goal);
        if (known == TECH_KNOWN) {
          research->tech_goal = A_UNSET;
        }
      }
    }
    changed = true;

    // Inform everybody about global advances
    send_game_info(nullptr);
    send_research_info(research, nullptr);
  }

  if (changed) {
    send_player_all_c(pplayer, nullptr);
  }
}

/**
   Handles vision editing requests from client.
 */
void handle_edit_player_vision(struct connection *pc, int plr_no, int tile,
                               bool known, int size)
{
  struct player *pplayer;
  struct tile *ptile_center;

  ptile_center = index_to_tile(&(wld.map), tile);
  if (!ptile_center) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot edit vision because %d is not a valid "
                  "tile index on this map!"),
                tile);
    return;
  }

  pplayer = player_by_number(plr_no);
  if (!pplayer) {
    notify_conn(pc->self, ptile_center, E_BAD_COMMAND, ftc_editor,
                // TRANS: ..." at <tile-coordinates> because"...
                _("Cannot edit vision for the tile at %s because "
                  "given player id %d is invalid."),
                tile_link(ptile_center), plr_no);
    return;
  }

  conn_list_do_buffer(game.est_connections);
  square_iterate(&(wld.map), ptile_center, size - 1, ptile)
  {
    if (!known) {
      struct city *pcity = tile_city(ptile);
      bool cannot_make_unknown = false;

      if (pcity && city_owner(pcity) == pplayer) {
        continue;
      }

      unit_list_iterate(ptile->units, punit)
      {
        if (unit_owner(punit) == pplayer
            || really_gives_vision(pplayer, unit_owner(punit))) {
          cannot_make_unknown = true;
          break;
        }
      }
      unit_list_iterate_end;

      if (cannot_make_unknown) {
        continue;
      }

      /* The client expects tiles which become unseen to
       * contain no units (client/packhand.c +2368).
       * So here we tell it to remove units that do
       * not give it vision. */
      unit_list_iterate(ptile->units, punit)
      {
        conn_list_iterate(pplayer->connections, pconn)
        {
          dsend_packet_unit_remove(pconn, punit->id);
        }
        conn_list_iterate_end;
      }
      unit_list_iterate_end;
    }

    if (known) {
      map_show_tile(pplayer, ptile);
    } else {
      map_hide_tile(pplayer, ptile);
    }
  }
  square_iterate_end;
  conn_list_do_unbuffer(game.est_connections);
}

/**
   Client editor requests us to recalculate borders. Note that this does
   not necessarily extend borders to their maximum due to the way the
   borders code is written. This may be considered a feature or limitation.
 */
void handle_edit_recalculate_borders(struct connection *pc)
{
  map_calculate_borders();
}

/**
   Remove any city at the given location.
 */
void handle_edit_city_remove(struct connection *pc, int id)
{
  struct city *pcity;

  pcity = game_city_by_number(id);
  if (pcity == nullptr) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("No such city (ID %d)."), id);
    return;
  }

  remove_city(pcity);
}

/**
   Run any pending tile checks.
 */
void handle_edit_check_tiles(struct connection *pc)
{
  check_edited_tile_terrains();
}

/**
   Temporarily remove fog-of-war for the player with player number 'plr_no'.
   This will only stay in effect while the server is in edit mode and the
   connection is editing. Has no effect if fog-of-war is disabled globally.
 */
void handle_edit_toggle_fogofwar(struct connection *pc, int plr_no)
{
  struct player *pplayer;

  if (!game.info.fogofwar) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot toggle fog-of-war when it is already "
                  "disabled."));
    return;
  }

  pplayer = player_by_number(plr_no);
  if (!pplayer) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Cannot toggle fog-of-war for invalid player ID %d."),
                plr_no);
    return;
  }

  conn_list_do_buffer(game.est_connections);
  if (unfogged_players[player_number(pplayer)]) {
    enable_fog_of_war_player(pplayer);
    unfogged_players[player_number(pplayer)] = false;
  } else {
    disable_fog_of_war_player(pplayer);
    unfogged_players[player_number(pplayer)] = true;
  }
  conn_list_do_unbuffer(game.est_connections);
}

/**
   Create or remove a start position at a tile.
 */
void handle_edit_startpos(struct connection *pconn,
                          const struct packet_edit_startpos *packet)
{
  struct tile *ptile = index_to_tile(&(wld.map), packet->id);
  bool changed;

  // Check.
  if (nullptr == ptile) {
    notify_conn(pconn->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Invalid tile index %d for start position."), packet->id);
    return;
  }

  // Handle.
  if (packet->removal) {
    changed = map_startpos_remove(ptile);
  } else {
    if (nullptr != map_startpos_get(ptile)) {
      changed = false;
    } else {
      map_startpos_new(ptile);
      changed = true;
    }
  }

  // Notify.
  if (changed) {
    conn_list_iterate(game.est_connections, aconn)
    {
      if (can_conn_edit(aconn)) {
        send_packet_edit_startpos(aconn, packet);
      }
    }
    conn_list_iterate_end;
  }
}

/**
   Setup which nations can start at a start position.
 */
void handle_edit_startpos_full(
    struct connection *pconn, const struct packet_edit_startpos_full *packet)
{
  struct tile *ptile = index_to_tile(&(wld.map), packet->id);
  struct startpos *psp;

  // Check.
  if (nullptr == ptile) {
    notify_conn(pconn->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Invalid tile index %d for start position."), packet->id);
    return;
  }

  psp = map_startpos_get(ptile);
  if (nullptr == psp) {
    notify_conn(pconn->self, ptile, E_BAD_COMMAND, ftc_editor,
                _("Cannot edit start position nations at (%d, %d) "
                  "because there is no start position there."),
                TILE_XY(ptile));
    return;
  }

  // Handle.
  if (startpos_unpack(psp, packet)) {
    // Notify.
    conn_list_iterate(game.est_connections, aconn)
    {
      if (can_conn_edit(aconn)) {
        send_packet_edit_startpos_full(aconn, packet);
      }
    }
    conn_list_iterate_end;
  }
}

/**
   Handle edit requests to the main game data structure.
 */
void handle_edit_game(struct connection *pc,
                      const struct packet_edit_game *packet)
{
  bool changed = false;

  if (packet->scenario != game.scenario.is_scenario) {
    game.scenario.is_scenario = packet->scenario;
    changed = true;
  }

  if (0 != strncmp(packet->scenario_name, game.scenario.name, 256)) {
    sz_strlcpy(game.scenario.name, packet->scenario_name);
    changed = true;
  }

  if (0
      != strncmp(packet->scenario_authors, game.scenario.authors,
                 sizeof(packet->scenario_authors))) {
    sz_strlcpy(game.scenario.authors, packet->scenario_authors);
    changed = true;
  }

  if (packet->scenario_random != game.scenario.save_random) {
    game.scenario.save_random = packet->scenario_random;
    changed = true;
  }

  if (packet->scenario_players != game.scenario.players) {
    game.scenario.players = packet->scenario_players;
    changed = true;
  }

  if (packet->startpos_nations != game.scenario.startpos_nations) {
    game.scenario.startpos_nations = packet->startpos_nations;
    changed = true;
  }

  if (packet->prevent_new_cities != game.scenario.prevent_new_cities) {
    game.scenario.prevent_new_cities = packet->prevent_new_cities;
    changed = true;
  }

  if (packet->lake_flooding != game.scenario.lake_flooding) {
    game.scenario.lake_flooding = packet->lake_flooding;
    changed = true;
  }

  if (packet->ruleset_locked != game.scenario.ruleset_locked) {
    game.scenario.ruleset_locked = packet->ruleset_locked;
    changed = true;
  }

  if (changed) {
    send_scenario_info(nullptr);
    send_game_info(nullptr);
  }
}

/**
   Handle edit requests to scenario description
 */
void handle_edit_scenario_desc(struct connection *pc,
                               const char *scenario_desc)
{
  if (0
      != strncmp(scenario_desc, game.scenario_desc.description,
                 MAX_LEN_PACKET)) {
    sz_strlcpy(game.scenario_desc.description, scenario_desc);
    send_scenario_description(nullptr);
  }
}

/**
   Make scenario file out of current game.
 */
void handle_save_scenario(struct connection *pc, const char *name)
{
  if (pc->access_level != ALLOW_HACK) {
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("No permissions to remotely save scenario."));
    return;
  }

  if (!game.scenario.is_scenario) {
    // Scenario information not available
    notify_conn(pc->self, nullptr, E_BAD_COMMAND, ftc_editor,
                _("Scenario information not set. Cannot save scenario."));
    return;
  }

  // Client initiated scenario saving is not handmade
  game.scenario.handmade = false;

  save_game(name, "Scenario", true);
}
