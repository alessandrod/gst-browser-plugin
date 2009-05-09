/*
 * Copyright (C) 2009 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro.d@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include "gbp-plugin.h"

GList *mime_types;
char *mime_types_description;

static void
invalidate_mime_types_description ()
{
  if (mime_types_description != NULL) {
    g_free (mime_types_description);
    mime_types_description = NULL;
  }
}

GList *
gbp_plugin_get_mime_types ()
{
  return mime_types;
}

gboolean
gbp_plugin_add_mime_type (const char *mime_type)
{
  GList *walk, *last = NULL, *tmp;

  g_return_val_if_fail (mime_type != NULL, FALSE);

  for (walk = mime_types; walk != NULL; walk = walk->next) {
    if (!strcmp ((const char *) walk->data, mime_type))
      return FALSE;

    last = walk;
  }

  tmp = g_list_append (last, (char *) mime_type);
  if (last == NULL)
    mime_types = tmp;

  invalidate_mime_types_description();

  return TRUE;
}

gboolean
gbp_plugin_remove_mime_type (const char *mime_type)
{
  GList *walk;

  for (walk = mime_types; walk != NULL; walk = walk->next) {
    if (!strcmp ((const char *) walk->data, mime_type)) {
      mime_types = g_list_delete_link (mime_types, walk);
  
      invalidate_mime_types_description();

      return TRUE;
    }
  }

  return FALSE;
}

void
gbp_plugin_remove_all_mime_types ()
{
  g_list_free (mime_types);
  mime_types = NULL;
  invalidate_mime_types_description();
}

char *
gbp_plugin_get_mime_types_description ()
{
  GList *walk;
  int i;
  gchar **mimes;

  if (mime_types_description != NULL)
    return mime_types_description;

  if (mime_types == NULL)
    return NULL;

  mimes = (gchar **) g_new (gchar *, g_list_length (mime_types));

  for (walk = mime_types, i=0 ; walk != NULL; walk = walk->next, ++i)
    mimes[i] = (gchar *) walk->data;

  mime_types_description = g_strjoinv(";", mimes);

  g_free (mimes);

  return mime_types_description;
}
