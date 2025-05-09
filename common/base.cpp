// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Freeciv21 and Freeciv Contributors

// self
#include "base.h"

// utility
#include "bitvector.h"
#include "log.h"

// common
#include "extras.h"
#include "fc_types.h"
#include "game.h"
#include "map.h"
#include "requirements.h"
#include "tile.h"
#include "unit.h"
#include "unittype.h"

// Qt
#include <QtPreprocessorSupport> // Q_UNUSED

/**
   Check if base provides effect
 */
bool base_has_flag(const struct base_type *pbase, enum base_flag_id flag)
{
  return BV_ISSET(pbase->flags, flag);
}

/**
   Returns TRUE iff any cardinally adjacent tile contains a base with
   the given flag (does not check ptile itself)
 */
bool is_base_flag_card_near(const struct tile *ptile, enum base_flag_id flag)
{
  extra_type_by_cause_iterate(EC_BASE, pextra)
  {
    if (base_has_flag(extra_base_get(pextra), flag)) {
      cardinal_adjc_iterate(&(wld.map), ptile, adjc_tile)
      {
        if (tile_has_extra(adjc_tile, pextra)) {
          return true;
        }
      }
      cardinal_adjc_iterate_end;
    }
  }
  extra_type_by_cause_iterate_end;

  return false;
}

/**
   Returns TRUE iff any adjacent tile contains a base with the given flag
   (does not check ptile itself)
 */
bool is_base_flag_near_tile(const struct tile *ptile, enum base_flag_id flag)
{
  extra_type_by_cause_iterate(EC_BASE, pextra)
  {
    if (base_has_flag(extra_base_get(pextra), flag)) {
      adjc_iterate(&(wld.map), ptile, adjc_tile)
      {
        if (tile_has_extra(adjc_tile, pextra)) {
          return true;
        }
      }
      adjc_iterate_end;
    }
  }
  extra_type_by_cause_iterate_end;

  return false;
}

/**
   Returns TRUE iff the given flag is retired.
 */
bool base_flag_is_retired(enum base_flag_id flag)
{
  Q_UNUSED(flag)
  /* No new flags retired in 3.1. Flags that were retired in 3.0 are already
   * completely removed. */
  return false;
}

/**
   Base provides base flag for unit? Checks if base provides flag and if
   base is native to unit.
 */
bool base_has_flag_for_utype(const struct base_type *pbase,
                             enum base_flag_id flag,
                             const struct unit_type *punittype)
{
  return base_has_flag(pbase, flag)
         && is_native_extra_to_utype(base_extra_get(pbase), punittype);
}

/**
   Can unit build base to given tile?
 */
bool can_build_base(const struct unit *punit, const struct base_type *pbase,
                    const struct tile *ptile)
{
  struct extra_type *pextra = base_extra_get(pbase);
  struct player *pplayer = unit_owner(punit);

  if (!can_build_extra_base(pextra, pplayer, ptile)) {
    return false;
  }

  return are_reqs_active(pplayer, tile_owner(ptile), nullptr, nullptr, ptile,
                         punit, unit_type_get(punit), nullptr, nullptr,
                         nullptr, &pextra->reqs, RPT_CERTAIN);
}

/**
   Returns base_type entry for an ID value.
 */
struct base_type *base_by_number(const Base_type_id id)
{
  struct extra_type_list *bases;

  bases = extra_type_list_by_cause(EC_BASE);

  if (bases == nullptr || id < 0 || id >= extra_type_list_size(bases)) {
    return nullptr;
  }

  return extra_base_get(extra_type_list_get(bases, id));
}

/**
   Return the base index.
 */
Base_type_id base_number(const struct base_type *pbase)
{
  fc_assert_ret_val(nullptr != pbase, 0);
  return pbase->item_number;
}

/**
   Return extra that base is.
 */
struct extra_type *base_extra_get(const struct base_type *pbase)
{
  return pbase->self;
}

/**
   Return the number of base_types.
 */
Base_type_id base_count() { return game.control.num_base_types; }

/**
   Initialize base_type structures.
 */
void base_type_init(struct extra_type *pextra, int idx)
{
  auto *pbase = new base_type;

  pextra->data.base = pbase;

  pbase->item_number = idx;
  pbase->self = pextra;
}

/**
   Free the memory associated with base types
 */
void base_types_free() {}

/**
   Get best gui_type base for given parameters
 */
struct base_type *get_base_by_gui_type(enum base_gui_type type,
                                       const struct unit *punit,
                                       const struct tile *ptile)
{
  extra_type_by_cause_iterate(EC_BASE, pextra)
  {
    struct base_type *pbase = extra_base_get(pextra);

    if (type == pbase->gui_type
        && (punit == nullptr || can_build_base(punit, pbase, ptile))) {
      return pbase;
    }
  }
  extra_type_by_cause_iterate_end;

  return nullptr;
}

/**
   Does this base type claim territory?
 */
bool territory_claiming_base(const struct base_type *pbase)
{
  return pbase->border_sq >= 0;
}
