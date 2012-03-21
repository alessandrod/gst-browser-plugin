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
#include "config.h"

#include "gbp-np-class.h"
#include <string.h>

GbpNPClass gbp_np_class;
#ifdef PLAYBACK_THREAD_POOL
static GThreadPool *playback_thread_pool;
#endif

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

static const char *playback_command_names[4] = {
  "STOP",
  "PAUSE",
  "START",
  "QUIT",
};

typedef struct
{
  PlaybackCommandCode code;
  NPPGbpData *data;
  GbpPlayer *player;
  gboolean free_data;
  GCond *cond;
  GMutex *lock;
  gboolean done;
  gboolean wait;
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
static bool gbp_np_class_property_have_audio_get (NPObject *obj,
    NPIdentifier name, NPVariant *result);
static bool gbp_np_class_property_have_audio_set (NPObject *obj,
    NPIdentifier name, const NPVariant *value);

PlaybackCommand *playback_command_new (PlaybackCommandCode code,
    NPPGbpData *data, gboolean free_data);
void playback_command_free (PlaybackCommand *command);
void playback_command_push (PlaybackCommandCode code,
    NPPGbpData *data, gboolean free_data, gboolean wait);
#ifndef PLAYBACK_THREAD_POOL
static gpointer playback_thread_func (gpointer data);
#else
static void playback_thread_pool_func (gpointer pool_data, gpointer push_data);
#endif
void gbp_np_class_start_playback_thread ();
void gbp_np_class_stop_playback_thread ();
static gint compare_commands(gconstpointer a, gconstpointer b, gpointer user_data);

/* cached method ids, allocated by gbp_np_class_init and destroyed by
 * gbp_np_class_free */
static NPIdentifier *method_identifiers;
static guint methods_num;
static NPIdentifier *property_identifiers;
static guint properties_num;
#ifndef PLAYBACK_THREAD_POOL
static GAsyncQueue *joinable_threads;
#endif

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
  {"have_audio", gbp_np_class_property_have_audio_get, gbp_np_class_property_have_audio_set, NULL},
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
gbp_np_class_invoke (NPObject *npobj, NPIdentifier name,
    const NPVariant *args, uint32_t argCount, NPVariant *result)
{
  guint i;
  GbpNPClassMethod *method;

  /* TODO: Find out why in Safari args == NULL */
  if (args == NULL) args = (NPVariant *) 0x11;

  g_return_val_if_fail (npobj != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);


  for (i = 0; i < methods_num; ++i) {
    if (name == method_identifiers[i]) {
      GbpNPObject *obj = (GbpNPObject *) npobj;
      NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

      method = &gbp_np_class_methods[i];
      GST_LOG_OBJECT (data->player,
          "calling javascript method %s", method->name);
      return method->method(npobj, name, args, argCount, result);
    }
  }

  NPN_SetException (npobj, "No method with this name exists.");
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
  playback_command_push (PLAYBACK_CMD_START, data, FALSE, FALSE);

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
  playback_command_push (PLAYBACK_CMD_STOP, data, FALSE, FALSE);

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
  playback_command_push (PLAYBACK_CMD_PAUSE, data, FALSE, FALSE);

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

  GST_DEBUG_OBJECT (data->player, "set state handler %p", data->stateHandler);
      

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

  GST_INFO_OBJECT (data->player, "setting uri %s", uri);
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

  if (value->type == NPVariantType_Int32)
    volume = NPVARIANT_TO_INT32 (*value);
  else if (value->type == NPVariantType_Double)
    volume = NPVARIANT_TO_DOUBLE (*value);
  else {
    NPN_SetException (npobj, "volume must be a number");
    return FALSE;
  }

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

  g_object_set (data->player, "volume", volume, NULL);

  return TRUE;
}

static bool gbp_np_class_property_have_audio_get (NPObject *npobj,
    NPIdentifier name, NPVariant *result)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;
  gboolean have_audio;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;

  g_object_get (data->player, "have_audio", &have_audio, NULL);

  BOOLEAN_TO_NPVARIANT (have_audio, *result);
  return TRUE;
}

static bool gbp_np_class_property_have_audio_set (NPObject *npobj,
    NPIdentifier name, const NPVariant *value)
{
  GbpNPObject *obj = (GbpNPObject *) npobj;
  gboolean have_audio;

  g_return_val_if_fail (obj != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (value->type == NPVariantType_Bool) {
    have_audio = NPVARIANT_TO_BOOLEAN (*value);
  } else {
    NPN_SetException (npobj, "have_audio must be a boolean");
    return FALSE;
  }

  NPPGbpData *data = (NPPGbpData *) obj->instance->pdata;
  g_object_set (data->player, "have_audio", have_audio, NULL);

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

  GST_DEBUG_CATEGORY_INIT (gbp_player_debug,
      "gbp-player", 0, "GStreamer Browser Plugin");

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

#ifdef PLAYBACK_THREAD_POOL
  playback_thread_pool = g_thread_pool_new (playback_thread_pool_func, NULL,
      PLAYBACK_THREAD_POOL_MAX_SIZE, FALSE, NULL);
  g_thread_pool_set_max_idle_time (PLAYBACK_THREAD_POOL_MAX_IDLE_TIME);
#else
  joinable_threads = g_async_queue_new ();
#endif
}

void
gbp_np_class_free ()
{
  NPClass *klass = (NPClass *) &gbp_np_class;

  g_return_if_fail (klass->structVersion != 0);

#ifdef PLAYBACK_THREAD_POOL
  g_thread_pool_free (playback_thread_pool, FALSE, TRUE);
  playback_thread_pool = NULL;
#else
  while (g_async_queue_length (joinable_threads)) {
    GThread *thread = (GThread *) g_async_queue_pop (joinable_threads);
    g_thread_join (thread);
  }

  g_async_queue_unref (joinable_threads);
  joinable_threads = NULL;
#endif

  NPN_MemFree (method_identifiers);

  memset (&gbp_np_class, 0, sizeof (GbpNPClass));
}

#ifndef PLAYBACK_THREAD_POOL
void gbp_np_class_start_object_playback_thread(NPPGbpData *data)
{
  data->playback_thread = g_thread_create (playback_thread_func,
      data->playback_queue, TRUE, NULL);
}
#endif

void gbp_np_class_stop_object_playback_thread(NPPGbpData *data)
{
  playback_command_push (PLAYBACK_CMD_QUIT, data, FALSE, TRUE);
}

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
  command->cond = g_cond_new ();
  command->lock = g_mutex_new ();
  command->done = FALSE;
  command->wait = FALSE;

  return command;
}

void
playback_command_free (PlaybackCommand *command)
{
  g_return_if_fail (command != NULL);

  GST_INFO ("freeing command %p", command);

  if (command->player)
    g_object_unref (command->player);

  if (command->free_data)
    npp_gbp_data_free (command->data);
  command->data = NULL;

  g_cond_free (command->cond);
  g_mutex_free (command->lock);
  g_free (command);
}

void
playback_command_push (PlaybackCommandCode code,
    NPPGbpData *data, gboolean free_data, gboolean wait)
{
  PlaybackCommand *command = NULL;
  g_return_if_fail (data != NULL);

  g_async_queue_lock (data->playback_queue);
  if (!data->exiting) {
    command = playback_command_new (code, data, free_data); 
    command->wait = wait;

    /* ref the queue, unref it in do_playback_queue so that we avoid a race
     * between destroying a plugin instance (which unrefs ->playback_queue), and
     * threads still running in the thread pool
     */
    g_async_queue_ref (data->playback_queue);
    g_async_queue_push_sorted_unlocked (data->playback_queue,
        command, compare_commands, NULL);

#ifdef PLAYBACK_THREAD_POOL
    if (g_atomic_int_exchange_and_add (&data->pending_commands, 1) == 0) {
      GST_INFO_OBJECT (data->player, "no pending commands, pushing worker");
      g_thread_pool_push (playback_thread_pool, data, NULL);
    }
#endif
  } else {
    GST_INFO_OBJECT (data->player, "exiting, ignoring %s",
        playback_command_names[code]);
  }
  g_async_queue_unlock (data->playback_queue);

  if (command && wait) {
    GbpPlayer *player = data->player;
    GST_INFO_OBJECT (player, "waiting for command %s to complete",
        playback_command_names[code]);
    g_mutex_lock (command->lock);
    while (!command->done)
      g_cond_wait (command->cond, command->lock);
    g_mutex_unlock (command->lock);
    GST_INFO_OBJECT (player, "command %s completed",
        playback_command_names[code]);
    playback_command_free (command);
  }
}

static gboolean
do_playback_command (PlaybackCommand *command)
{
  GbpPlayer *player;
  gboolean exit = FALSE;  

  if (command->player)
    player = command->player;
  else
    player = NULL;

  GST_DEBUG_OBJECT (player, "pool worker %p processing command %s",
      g_thread_self(), playback_command_names[command->code]);

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
  GST_DEBUG_OBJECT (player, "pool worker %p processed command %s",
      g_thread_self(), playback_command_names[command->code]);

  /* signal in case someone is waiting */
  g_mutex_lock (command->lock);
  command->done = TRUE;
  g_cond_signal (command->cond);
  g_mutex_unlock (command->lock);

  return exit;
}

static gboolean
do_playback_queue (NPPGbpData *data, GAsyncQueue *queue, GTimeVal *timeout)
{
  PlaybackCommand *command, *flushed_command;
  gboolean exit = FALSE;
  gboolean free_data = FALSE;

  while (exit == FALSE) {
    command = g_async_queue_timed_pop (queue, timeout);
    if (command == NULL)
      break;

    free_data = !command->wait;
    exit = do_playback_command (command);

    g_async_queue_lock (queue);
    if (exit) {
      while (g_async_queue_length_unlocked (queue)) {
        flushed_command = g_async_queue_pop_unlocked (queue);
        g_async_queue_unref (queue);
        playback_command_free (flushed_command);
      }

      data->exiting = TRUE;
    }

#ifdef PLAYBACK_THREAD_POOL
    if (g_atomic_int_dec_and_test (&data->pending_commands))
      ;
#endif
    
    g_async_queue_unlock (queue);
    /* we ref the queue for each queued command, see playback_command_push  */
    g_async_queue_unref (queue);
    if (free_data)
      playback_command_free (command);
  }

  return exit;
}

#ifndef PLAYBACK_THREAD_POOL
static gpointer
playback_thread_func (gpointer data)
{
  GAsyncQueue *queue = (GAsyncQueue *) data;

  /* loop over the queue forever */
  do_playback_queue (NULL, queue, NULL);

  g_async_queue_push (joinable_threads, g_thread_self ());

  return NULL;
}
#else
static void
playback_thread_pool_func (gpointer push_data, gpointer pull_data)
{
  NPPGbpData *data = (NPPGbpData *) push_data;
  GTimeVal timeout;
  GbpPlayer *player = NULL;

  timeout.tv_sec = 0;
  timeout.tv_usec = 100;

  if (data->player != NULL)
    player = data->player;

  GST_DEBUG_OBJECT (player, "pool worker %p starting on player %p",
      g_thread_self (), player);

  do_playback_queue (data, data->playback_queue, &timeout);

  GST_DEBUG_OBJECT (player, "pool worker %p done on player %p",
      g_thread_self (), player);
}
#endif

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
