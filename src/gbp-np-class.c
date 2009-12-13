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
#include "gbp-np-class.h"
#include <string.h>

GbpNPClass gbp_np_class;

typedef struct
{
  const char *name;
  NPInvokeFunctionPtr method;
} GbpNPClassMethod;

typedef struct
{
  const char *name;
  NPGetPropertyFunctionPtr get;
  NPSetPropertyFunctionPtr set;
  NPRemovePropertyFunctionPtr remove;
} GbpNPClassProperty;

typedef enum
{
  PLAYBACK_CMD_STOP,
  PLAYBACK_CMD_PAUSE,
  PLAYBACK_CMD_START,
  PLAYBACK_CMD_QUIT,
} PlaybackCommandCode;

typedef struct
{
  PlaybackCommandCode code;
  NPPGbpData *data;
  GbpPlayer *player;
  gboolean free_data;
} PlaybackCommand;

static bool gbp_np_class_method_start (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_stop (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_pause (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_set_error_handler (NPObject *obj,
    NPIdentifier name, const NPVariant *args, uint32_t argCount,
    NPVariant *result);
static bool gbp_np_class_method_set_state_handler (NPObject *obj,
    NPIdentifier name, const NPVariant *args, uint32_t argCount,
    NPVariant *result);
static bool gbp_np_class_method_get_duration (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_get_position (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);
static bool gbp_np_class_method_seek (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result);

static bool gbp_np_class_property_generic_get (NPObject *obj,
    NPIdentifier name, NPVariant *result);
static bool gbp_np_class_property_generic_set (NPObject *obj,
    NPIdentifier name, const NPVariant *value);
static bool gbp_np_class_property_generic_remove (NPObject *obj,
    NPIdentifier name);
static bool gbp_np_class_property_state_get (NPObject *obj,
    NPIdentifier name, NPVariant *result);
static bool gbp_np_class_property_uri_get (NPObject *obj,
    NPIdentifier name, NPVariant *result);
static bool gbp_np_class_property_uri_set (NPObject *obj,
    NPIdentifier name, const NPVariant *value);
static bool gbp_np_class_property_volume_get (NPObject *obj,
    NPIdentifier name, NPVariant *result);
static bool gbp_np_class_property_volume_set (NPObject *obj,
    NPIdentifier name, const NPVariant *value);

PlaybackCommand *playback_command_new (PlaybackCommandCode code,
    NPPGbpData *data, gboolean free_data);
void playback_command_free (PlaybackCommand *command);
void playback_command_push (PlaybackCommandCode code,
    NPPGbpData *data, gboolean free_data);
gpointer playback_thread_func (gpointer data);
void gbp_np_class_start_playback_thread ();
void gbp_np_class_stop_playback_thread ();
static gint compare_commands(gconstpointer a, gconstpointer b, gpointer user_data);

/* cached method ids, allocated by gbp_np_class_init and destroyed by
 * gbp_np_class_free */
static NPIdentifier *method_identifiers;
static guint methods_num;
static NPIdentifier *property_identifiers;
static guint properties_num;
static GAsyncQueue *joinable_threads;

static GbpNPClassMethod gbp_np_class_methods[] = {
  {"start", gbp_np_class_method_start},
  {"stop", gbp_np_class_method_stop},
  {"pause", gbp_np_class_method_pause},
  {"get_duration", gbp_np_class_method_get_duration},
  {"get_position", gbp_np_class_method_get_position},
  {"seek", gbp_np_class_method_seek},
  {"setErrorHandler", gbp_np_class_method_set_error_handler},
  {"setStateHandler", gbp_np_class_method_set_state_handler},

  /* sentinel */
  {NULL, NULL}
};

static GbpNPClassProperty gbp_np_class_properties[] = {
  {"state", gbp_np_class_property_state_get, NULL, NULL},
  {"uri", gbp_np_class_property_uri_get, gbp_np_class_property_uri_set, NULL},
  {"volume", gbp_np_class_property_volume_get, gbp_np_class_property_volume_set, NULL},
  /* sentinel */
  {NULL, NULL}
};

static NPObject *
gbp_np_class_allocate (NPP npp, NPClass *aClass)
{
  GbpNPObject *obj = (GbpNPObject *) NPN_MemAlloc (sizeof (GbpNPObject));
  obj->instance = npp;

  return (NPObject *) obj;
}

static void
gbp_np_class_deallocate (NPObject *npobj)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  obj->instance = NULL;
  NPN_MemFree (obj);
}

static bool
gbp_np_class_has_method (NPObject *obj, NPIdentifier name)
{
  guint i;

  for (i = 0; i < methods_num; ++i) {
    if (name == method_identifiers[i])
      return TRUE;
  }

  return FALSE;
}

static bool
gbp_np_class_invoke (NPObject *obj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  guint i;

  for (i = 0; i < methods_num; ++i) {
    if (name == method_identifiers[i]) {

      return gbp_np_class_methods[i].method(obj, name,
          args, argCount, result);
    }
  }

  NPN_SetException (obj, "No method with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_has_property (NPObject *obj, NPIdentifier name)
{
  guint i;

  for (i = 0; i < properties_num; ++i) {
    if (name == property_identifiers[i])
      return TRUE;
  }

  return FALSE;
}

static bool
gbp_np_class_get_property (NPObject *obj, NPIdentifier name, NPVariant *result)
{
  gint i;

  for (i = 0; i < properties_num; ++i) {
    if (name == property_identifiers[i])
      return gbp_np_class_properties[i].get(obj, name, result);
  }

  NPN_SetException (obj, "No property with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_set_property (NPObject *obj, NPIdentifier name, const NPVariant *value)
{
  gint i;

  for (i = 0; i < properties_num; ++i) {
    if (name == property_identifiers[i])
      return gbp_np_class_properties[i].set(obj, name, value);
  }

  NPN_SetException (obj, "No property with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_remove_property (NPObject *obj, NPIdentifier name)
{
  gint i;

  for (i = 0; i < properties_num; ++i) {
    if (name == property_identifiers[i])
      return gbp_np_class_properties[i].remove(obj, name);
  }

  NPN_SetException (obj, "No property with this name exists.");
  return FALSE;
}

static bool
gbp_np_class_enumerate (NPObject *obj, NPIdentifier **value, uint32_t *count)
{
  return FALSE;
}

static bool
gbp_np_class_method_start (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  playback_command_push (PLAYBACK_CMD_START, data, FALSE);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_stop (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  playback_command_push (PLAYBACK_CMD_STOP, data, FALSE);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_pause (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  playback_command_push (PLAYBACK_CMD_PAUSE, data, FALSE);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_get_duration (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GstClockTime duration;
  guint32 res;
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  duration = gbp_player_get_duration (data->player);
  if (duration == GST_CLOCK_TIME_NONE)
    res = 0;
  else
    res = duration / GST_MSECOND;

  INT32_TO_NPVARIANT (res, *result);
  return TRUE;
}

static bool
gbp_np_class_method_get_position (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GstClockTime position;
  guint32 res;
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  position = gbp_player_get_position (data->player);
  if (position == GST_CLOCK_TIME_NONE)
    res = 0;
  else
    res = position / GST_MSECOND;

  INT32_TO_NPVARIANT (res, *result);
  return TRUE;
}

static bool
gbp_np_class_method_seek (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GstClockTime position;
  gdouble rate = 1.0;
  gboolean res;
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  if (argCount < 1 || argCount > 2) {
    NPN_SetException (npobj, "invalid number of arguments");

    return FALSE;
  }

  if (args[0].type == NPVariantType_Int32) {
    position = args[0].value.intValue * GST_MSECOND;
  } else {
    NPN_SetException (npobj, "position must be an integer");

    return FALSE;
  }

  if (argCount == 2) {
    if (args[1].type == NPVariantType_Double) {
      rate = args[1].value.doubleValue;
    } else if (args[1].type == NPVariantType_Int32) {
      rate = args[1].value.intValue;
    } else {
      NPN_SetException (npobj, "rate must be a double");

      return FALSE;
    }
  }

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  res = gbp_player_seek (data->player, position, rate);

  BOOLEAN_TO_NPVARIANT (res, *result);
  return TRUE;
}

static bool
gbp_np_class_method_set_error_handler (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (argCount == 1, FALSE);
  g_return_val_if_fail (args[0].type == NPVariantType_Object, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  data->errorHandler = NPN_RetainObject(args[0].value.objectValue);

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_method_set_state_handler (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (argCount == 1, FALSE);
  g_return_val_if_fail (args[0].type == NPVariantType_Object, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  data->stateHandler = NPN_RetainObject(args[0].value.objectValue);

  g_print ("set state handler %p on instance %p\n",
      data->stateHandler, data);
      

  VOID_TO_NPVARIANT (*result);
  return TRUE;
}

static bool
gbp_np_class_property_generic_get (NPObject *obj,
    NPIdentifier name, NPVariant *result)
{
  NPN_SetException (obj, "Can't get property value");
  return FALSE;
}

static bool
gbp_np_class_property_generic_set (NPObject *obj,
    NPIdentifier name, const NPVariant *result)
{
  NPN_SetException (obj, "Can't set property value");
  return FALSE;
}

static bool
gbp_np_class_property_generic_remove (NPObject *obj,
    NPIdentifier name)
{
  NPN_SetException (obj, "Can't remove property");
  return FALSE;
}

static bool gbp_np_class_property_state_get (NPObject *npobj,
    NPIdentifier name, NPVariant *result)
{
  char *state_copy;
  GbpNPObject *obj = (GbpNPObject *) npobj;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

  state_copy = (char *) NPN_MemAlloc (strlen (data->state) + 1);
  strcpy (state_copy, data->state);

  STRINGZ_TO_NPVARIANT (state_copy, *result);
  return TRUE;
}

static bool gbp_np_class_property_uri_get (NPObject *npobj,
    NPIdentifier name, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;
  char *uri;
  char *uri_copy;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

  g_object_get (data->player, "uri", &uri, NULL);

  uri_copy = (char *) NPN_MemAlloc (strlen (uri) + 1);
  strcpy (uri_copy, uri);

  g_free (uri);

  STRINGZ_TO_NPVARIANT (uri_copy, *result);
  return TRUE;
}

static bool gbp_np_class_property_uri_set (NPObject *npobj,
    NPIdentifier name, const NPVariant *value)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;
  const char *uri;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  uri = NPVARIANT_TO_STRING (*value).UTF8Characters;

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

  g_object_set (data->player, "uri", uri, NULL);

  return TRUE;
}

static bool gbp_np_class_property_volume_get (NPObject *npobj,
    NPIdentifier name, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;
  gdouble volume;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

  g_object_get (data->player, "volume", &volume, NULL);

  DOUBLE_TO_NPVARIANT (volume, *result);
  return TRUE;
}

static bool gbp_np_class_property_volume_set (NPObject *npobj,
    NPIdentifier name, const NPVariant *value)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;
  gdouble volume;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  volume = NPVARIANT_TO_DOUBLE (*value);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

  g_object_set (data->player, "volume", volume, NULL);

  return TRUE;
}

void
gbp_np_class_init ()
{
  int i;
  const char **method_names, **property_names;

  NPClass *klass = (NPClass *) &gbp_np_class;

  /* only init once */
  g_return_if_fail (klass->structVersion == 0);

  klass->structVersion = NP_CLASS_STRUCT_VERSION;
  klass->allocate = gbp_np_class_allocate;
  klass->deallocate = gbp_np_class_deallocate;
  klass->hasMethod = gbp_np_class_has_method;
  klass->invoke = gbp_np_class_invoke;
  klass->hasProperty = gbp_np_class_has_property;
  klass->getProperty = gbp_np_class_get_property;
  klass->setProperty = gbp_np_class_set_property;
  klass->removeProperty = gbp_np_class_remove_property;
  klass->enumerate = gbp_np_class_enumerate;

  /* setup method identifiers */
  methods_num = \
      (sizeof (gbp_np_class_methods) / sizeof (GbpNPClassMethod)) - 1;

  /* alloc method_identifiers */
  method_identifiers = \
      (NPIdentifier *) NPN_MemAlloc (sizeof (NPIdentifier) * methods_num);

  method_names = (const char **) NPN_MemAlloc (sizeof (char *) * methods_num);
  for (i = 0; gbp_np_class_methods[i].name != NULL; ++i)
    method_names[i] = gbp_np_class_methods[i].name;
  
  /* setup property identifiers */
  properties_num = \
      (sizeof (gbp_np_class_properties) / sizeof (GbpNPClassProperty)) - 1;

  /* alloc property_identifiers */
  property_identifiers = \
      (NPIdentifier *) NPN_MemAlloc (sizeof (NPIdentifier) * properties_num);

  property_names = (const char **) NPN_MemAlloc (sizeof (char *) * properties_num);
  for (i = 0; gbp_np_class_properties[i].name != NULL; ++i) {
    property_names[i] = gbp_np_class_properties[i].name;
    if (gbp_np_class_properties[i].get == NULL)
      gbp_np_class_properties[i].get = gbp_np_class_property_generic_get;
    if (gbp_np_class_properties[i].set == NULL)
      gbp_np_class_properties[i].set = gbp_np_class_property_generic_set;
    if (gbp_np_class_properties[i].remove == NULL)
      gbp_np_class_properties[i].remove = gbp_np_class_property_generic_remove;
  }

  /* fill method_identifiers */
  NPN_GetStringIdentifiers (method_names, methods_num, method_identifiers);
  /* fill property identifiers */
  NPN_GetStringIdentifiers (property_names, properties_num, property_identifiers);

  NPN_MemFree (method_names);
  NPN_MemFree (property_names);

  joinable_threads = g_async_queue_new ();

#ifdef PLAYBACK_THREAD_SINGLE
  gbp_np_class_start_playback_thread ();
#endif
}

void
gbp_np_class_free ()
{
  NPClass *klass = (NPClass *) &gbp_np_class;

  g_return_if_fail (klass->structVersion != 0);

#ifdef PLAYBACK_THREAD_SINGLE
  gbp_np_class_stop_playback_thread ();
#endif

  g_print ("~ %d threads to join\n", g_async_queue_length (joinable_threads));
  while (g_async_queue_length (joinable_threads)) {
    GThread *thread = (GThread *) g_async_queue_pop (joinable_threads);
    g_print ("joining thread %p\n", thread);
    g_thread_join (thread);
  }

  g_async_queue_unref (joinable_threads);
  joinable_threads = NULL;

  NPN_MemFree (method_identifiers);

  memset (&gbp_np_class, 0, sizeof (GbpNPClass));
}

#ifndef PLAYBACK_THREAD_SINGLE
void gbp_np_class_start_object_playback_thread(NPPGbpData *data)
{
  data->playback_queue = g_async_queue_new ();
  data->playback_thread = g_thread_create (playback_thread_func,
      data->playback_queue, TRUE, NULL);
}

void gbp_np_class_stop_object_playback_thread(NPPGbpData *data)
{
  playback_command_push (PLAYBACK_CMD_QUIT, data, TRUE);
}

#else

void
gbp_np_class_start_playback_thread ()
{
  g_return_if_fail (gbp_np_class.playback_thread == NULL);

  gbp_np_class.playback_queue = g_async_queue_new ();
  gbp_np_class.playback_thread = g_thread_create (playback_thread_func,
      gbp_np_class.playback_queue, TRUE, NULL);
}

void
gbp_np_class_stop_playback_thread ()
{
  g_return_if_fail (gbp_np_class.playback_thread != NULL);

  playback_command_push (PLAYBACK_CMD_QUIT, NULL, FALSE);
}
#endif /* PLAYBACK_THREAD_SINGLE */

PlaybackCommand *
playback_command_new (PlaybackCommandCode code,
    NPPGbpData *data, gboolean free_data)
{
  PlaybackCommand *command = g_new0 (PlaybackCommand, 1);
  if (data)
    command->player = g_object_ref (data->player);
  else
    command->player = NULL;
  command->code = code;
  command->data = data;
  command->free_data = free_data;

  return command;
}

void
playback_command_free (PlaybackCommand *command)
{
  g_return_if_fail (command != NULL);

  if (command->player)
    g_object_unref (command->player);

  if (command->free_data)
    npp_gbp_data_free (command->data);
  command->data = NULL;

  g_free (command);
}

void
playback_command_push (PlaybackCommandCode code,
    NPPGbpData *data, gboolean free_data)
{
  GAsyncQueue *playback_queue;

#ifndef PLAYBACK_THREAD_SINGLE
  g_return_if_fail (data != NULL);
  playback_queue = data->playback_queue;
#else
  playback_queue = gbp_np_class.playback_queue;
#endif

  PlaybackCommand *command = playback_command_new (code, data, free_data);
  g_async_queue_push_sorted (playback_queue, command, compare_commands, NULL);
}

gpointer
playback_thread_func (gpointer data)
{
  GAsyncQueue *queue = (GAsyncQueue *) data;
  PlaybackCommand *command;
  GbpPlayer *player;
  gboolean exit = FALSE;

  while (exit == FALSE) {
    command = g_async_queue_pop (queue);

    if (command->player)
      player = command->player;
    else
      player = NULL;

    g_print ("player %p processing command %d\n", player, command->code);
    switch (command->code) {
      case PLAYBACK_CMD_STOP:
        gbp_player_stop (player);
        break;

      case PLAYBACK_CMD_PAUSE:
        gbp_player_pause (player);
        break;

      case PLAYBACK_CMD_START:
        gbp_player_start (player);
        break;

      case PLAYBACK_CMD_QUIT:
        gbp_player_stop (player);
        exit = TRUE;
        break;

      default:
        g_warn_if_reached ();
    }

    playback_command_free (command);
    command = NULL;
  }

  while (g_async_queue_length (queue)) {
    command = g_async_queue_pop (queue);
    playback_command_free (command);
  }

  g_async_queue_unref (queue);
  g_async_queue_push (joinable_threads, g_thread_self ());

  return NULL;
}

static gint
compare_commands(gconstpointer a, gconstpointer b, gpointer user_data)
{
  gint res;
  PlaybackCommand *cmd1 = (PlaybackCommand *) a;
  PlaybackCommand *cmd2 = (PlaybackCommand *) b;

  if (cmd1->code == PLAYBACK_CMD_QUIT)
    res = -1;
  else if (cmd2->code == PLAYBACK_CMD_QUIT)
    res = 1;
  else
    res = 0;

  return res;
}
