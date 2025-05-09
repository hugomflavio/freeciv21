// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Freeciv and Freeciv21 Contributors

// self
#include "server_settings.h"

// utility
#include "fcintl.h"
#include "log.h"

// common
#include "fc_interface.h"
#include "fc_types.h"

// Qt
#include <QString>

/**
   Returns the server setting with the specified name.
 */
server_setting_id server_setting_by_name(const char *name)
{
  fc_assert_ret_val(fc_funcs, SERVER_SETTING_NONE);
  fc_assert_ret_val(fc_funcs->server_setting_by_name, SERVER_SETTING_NONE);

  return fc_funcs->server_setting_by_name(name);
}

/**
   Returns the name of the server setting with the specified id.
 */
const char *server_setting_name_get(server_setting_id id)
{
  fc_assert_ret_val(fc_funcs, nullptr);
  fc_assert_ret_val(fc_funcs->server_setting_name_get, nullptr);

  return fc_funcs->server_setting_name_get(id);
}

/**
   Returns the type of the server setting with the specified id.
 */
enum sset_type server_setting_type_get(server_setting_id id)
{
  fc_assert_ret_val(fc_funcs, sset_type_invalid());
  fc_assert_ret_val(fc_funcs->server_setting_type_get, sset_type_invalid());

  return fc_funcs->server_setting_type_get(id);
}

/**
   Returns TRUE iff a server setting with the specified id exists.
 */
bool server_setting_exists(server_setting_id id)
{
  return sset_type_is_valid(server_setting_type_get(id));
}

/**
   Returns the value of the server setting with the specified id.
 */
bool server_setting_value_bool_get(server_setting_id id)
{
  fc_assert_ret_val(fc_funcs, false);
  fc_assert_ret_val(fc_funcs->server_setting_val_bool_get, false);
  fc_assert_ret_val(server_setting_type_get(id) == SST_BOOL, false);

  return fc_funcs->server_setting_val_bool_get(id);
}

/**
   Returns the value of the server setting with the specified id.
 */
int server_setting_value_int_get(server_setting_id id)
{
  fc_assert_ret_val(fc_funcs, false);
  fc_assert_ret_val(fc_funcs->server_setting_val_int_get, false);
  fc_assert_ret_val(server_setting_type_get(id) == SST_INT, false);

  return fc_funcs->server_setting_val_int_get(id);
}

/**
   Returns a server setting - value pair from its setting and value;
 */
ssetv ssetv_from_values(server_setting_id setting, int value)
{
  /* Only Boolean and TRUE can be supported unless setting value encoding
   * is implemented. */
  if (!static_cast<bool>(value)) {
    fc_assert(value == true);
    return SSETV_NONE;
  }

  /* Only Boolean settings can be supported unless the setting value is
   * encoded with the setting id. */
  if (server_setting_type_get(setting) != SST_BOOL) {
    fc_assert(server_setting_type_get((server_setting_id) setting)
              == SST_BOOL);
    return SSETV_NONE;
  }

  return static_cast<ssetv>(setting);
}

/**
   Returns the server setting of the setting - value pair.
 */
server_setting_id ssetv_setting_get(ssetv enc)
{
  /* Only Boolean settings can be supported unless the setting value is
   * encoded with the setting id. */
  fc_assert(server_setting_type_get((server_setting_id) enc) == SST_BOOL);

  return static_cast<server_setting_id>(enc);
}

/**
   Returns the server setting - value pair encoded in the string.
 */
ssetv ssetv_by_rule_name(const char *name)
{
  ssetv val = static_cast<ssetv>(server_setting_by_name(name));

  /* Only Boolean settings can be supported unless the setting value is
   * encoded with the setting id. */
  if (server_setting_type_get(static_cast<server_setting_id>(val))
      != SST_BOOL) {
    return SSETV_NONE;
  }

  return val;
}

/**
   Returns the server setting - value pair encoded as a string.
 */
const char *ssetv_rule_name(ssetv val)
{
  /* Only Boolean settings can be supported unless the setting value is
   * encoded with the setting id. */
  fc_assert(server_setting_type_get((server_setting_id) val) == SST_BOOL);

  return server_setting_name_get(static_cast<server_setting_id>(val));
}

/**
   Returns the server setting - value pair formated in a user readable way.
 */
QString ssetv_human_readable(ssetv val, bool present)
{
  QString out;

  /* Only Boolean settings can be supported unless the setting value is
   * encoded with the setting id. */
  fc_assert(server_setting_type_get((server_setting_id) val) == SST_BOOL);

  /* TRANS: the first %s is a server setting, the second %s is it's value.
   * Example: killstack is enabled */
  out =
      QString(_("%1 is %2"))
          .arg(server_setting_name_get(static_cast<server_setting_id>(val)),
               present ? _("enabled") : _("disabled"));

  return qUtf8Printable(out);
}

/**
   Returns if the server setting currently has the value in the pair.
 */
bool ssetv_setting_has_value(ssetv val)
{
  /* Only boolean settings can be supported unless the setting value is
   * encoded with the setting id. */
  fc_assert_ret_val(
      server_setting_type_get((server_setting_id) val) == SST_BOOL, false);

  return server_setting_value_bool_get(static_cast<server_setting_id>(val));
}
