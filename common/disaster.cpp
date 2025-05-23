// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Freeciv21 and Freeciv Contributors

// self
#include "disaster.h"

// utility
#include "bitvector.h"
#include "log.h"
#include "shared.h"

// common
#include "city.h"
#include "fc_types.h"
#include "game.h"
#include "name_translation.h"
#include "requirements.h"

static struct disaster_type disaster_types[MAX_DISASTER_TYPES];

/**
   Initialize disaster_type structures.
 */
void disaster_types_init()
{
  int i;

  for (i = 0; i < ARRAY_SIZE(disaster_types); i++) {
    disaster_types[i].id = i;
    requirement_vector_init(&disaster_types[i].reqs);
  }
}

/**
   Free the memory associated with disaster types
 */
void disaster_types_free()
{
  disaster_type_iterate(pdis) { requirement_vector_free(&pdis->reqs); }
  disaster_type_iterate_end;
}

/**
   Return the disaster id.
 */
Disaster_type_id disaster_number(const struct disaster_type *pdis)
{
  fc_assert_ret_val(nullptr != pdis, 0);

  return pdis->id;
}

/**
   Return the disaster index.

   Currently same as disaster_number()
   indicates use as an array index.
 */
Disaster_type_id disaster_index(const struct disaster_type *pdis)
{
  fc_assert_ret_val(nullptr != pdis, 0);

  return pdis - disaster_types;
}

/**
   Return disaster type of given id.
 */
struct disaster_type *disaster_by_number(Disaster_type_id id)
{
  fc_assert_ret_val(id >= 0 && id < game.control.num_disaster_types,
                    nullptr);

  return &disaster_types[id];
}

/**
   Return translated name of this disaster type.
 */
const char *disaster_name_translation(struct disaster_type *pdis)
{
  return name_translation_get(&pdis->name);
}

/**
   Return untranslated name of this disaster type.
 */
const char *disaster_rule_name(struct disaster_type *pdis)
{
  return rule_name_get(&pdis->name);
}

/**
   Check if disaster provides effect
 */
bool disaster_has_effect(const struct disaster_type *pdis,
                         enum disaster_effect_id effect)
{
  return BV_ISSET(pdis->effects, effect);
}

/**
   Whether disaster can happen in given city.
 */
bool can_disaster_happen(const struct disaster_type *pdis,
                         const struct city *pcity)
{
  return are_reqs_active(city_owner(pcity), nullptr, pcity, nullptr,
                         city_tile(pcity), nullptr, nullptr, nullptr,
                         nullptr, nullptr, &pdis->reqs, RPT_POSSIBLE);
}
