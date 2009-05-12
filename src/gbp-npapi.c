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

#include "gbp-npapi.h"
#include "gbp-plugin.h"
#include "gbp-np-class.h"
#include <string.h>


typedef struct _InvokeData {
  NPP instance;
  NPObject *object;
  NPVariant *args;
  int n_args;
} InvokeData;

/* FIXME: hack to deal with braindead state notification API */
typedef struct _StateClosure {
  NPP instance;
  const char *state;
} StateClosure;


void  invoke_data_free (InvokeData *invoke_data,
    gboolean remove_from_pending_slist);
void on_error_cb (GbpPlayer *player, GError *error, const char *debug,
    gpointer user_data);
void on_state_cb (GbpPlayer *player, gpointer user_data);

NPError NP_GetValue (NPP instance, NPPVariable variable, void *ret_value);
NPError NP_SetValue (NPP instance, NPNVariable variable, void *ret_value);


StateClosure state1, state2, state3;

NPNetscapeFuncs NPNFuncs;

GStaticMutex pending_invoke_data_lock = G_STATIC_MUTEX_INIT;
static GSList *pending_invoke_data;

/* NPP vtable symbols */
NPError
NPP_New (NPMIMEType plugin_type, NPP instance, uint16_t mode,
    int16_t argc, char *argn[], char *argv[], NPSavedData *saved_data)
{
  GbpPlayer *player;
  NPPGbpData *pdata;
  char *uri = NULL;
  guint width = 0, height = 0;
  int i;

  if (!instance)
    return NPERR_INVALID_INSTANCE_ERROR;

  for (i = 0; i < argc; ++i) {
    if (!strcmp (argn[i], "x-gbp-uri"))
      uri = argv[i];
    else if (!strcmp (argn[i], "width"))
      width = atoi (argv[i]);
    else if (!strcmp (argn[i], "height"))
      height = atoi (argv[i]);
  }

  if (uri == NULL || width == 0 || height == 0)
    return NPERR_INVALID_PARAM;

  g_type_init ();
  gst_init (NULL, NULL);

  player = (GbpPlayer *) g_object_new (GBP_TYPE_PLAYER, NULL);
  if (player == NULL)
    return NPERR_OUT_OF_MEMORY_ERROR;

  g_object_set (G_OBJECT (player), "width", width, "height", height,
      "uri", uri, NULL);

  pdata = (NPPGbpData *) NPN_MemAlloc (sizeof (NPPGbpData));
  pdata->player = player;
  pdata->errorHandler = NULL;
  pdata->stateHandler = NULL;

  g_object_connect (G_OBJECT (player), "signal::error",
      G_CALLBACK (on_error_cb), instance, NULL);

  state1.instance = state2.instance = state3.instance = instance;
  state1.state = "PLAYING";
  state2.state = "PAUSED";
  state2.state = "STOPPED";
  g_object_connect (G_OBJECT (player),
      "signal::playing", G_CALLBACK (on_state_cb), &state1,
      "signal::paused", G_CALLBACK (on_state_cb), &state2,
      "signal::stopped", G_CALLBACK (on_state_cb), &state3,
      NULL);

  instance->pdata = pdata;

  return NPERR_NO_ERROR;
}

NPError
NPP_Destroy (NPP instance, NPSavedData **saved_data)
{
  if (!instance)
    return NPERR_INVALID_INSTANCE_ERROR;

  NPPGbpData *data = (NPPGbpData *) instance->pdata;

  gbp_player_stop (data->player);
  g_object_unref (data->player);

  if (data->errorHandler != NULL)
    NPN_ReleaseObject (data->errorHandler);

  if (data->stateHandler != NULL)
    NPN_ReleaseObject (data->stateHandler);

  NPN_MemFree (data);

  return NPERR_NO_ERROR;
}

NPError
NPP_SetWindow (NPP instance, NPWindow *window)
{
  if (!instance)
    return NPERR_INVALID_INSTANCE_ERROR;

  NPPGbpData *data = (NPPGbpData *) instance->pdata;
  g_object_set (data->player, "xid", (gulong) window->window, NULL);

  return NPERR_NO_ERROR;
}

NPError
NPP_NewStream (NPP instance, NPMIMEType type,
    NPStream* stream, NPBool seekable, uint16_t* stype)
{
  return NPERR_GENERIC_ERROR;
}

NPError NPP_DestroyStream (NPP instance, NPStream* stream, NPReason reason)
{
  return NPERR_GENERIC_ERROR;
}

int32_t
NPP_WriteReady (NPP instance, NPStream* stream)
{
  return G_MAXINT32;
}

int32_t
NPP_Write (NPP instance, NPStream* stream,
    int32_t offset, int32_t len, void* buffer)
{
  return len;
}

void
NPP_StreamAsFile (NPP instance, NPStream* stream, const char* fname)
{
}

void
NPP_Print (NPP instance, NPPrint* platformPrint)
{
}

int16_t
NPP_HandleEvent (NPP instance, void* event)
{
  /* return not handled for now */
  return 0;
}

void
NPP_URLNotify (NPP instance, const char* url,
    NPReason reason, void* notifyData)
{
}

NPError
NPP_GetValue (NPP instance, NPPVariable variable, void *ret_value)
{
  return NP_GetValue (instance, variable, ret_value);
}

NPError
NPP_SetValue (NPP instance, NPNVariable variable, void *ret_value)
{
  return NP_SetValue (instance, variable, ret_value);
}

/* dlopen'd symbols */
char *
NP_GetMIMEDescription()
{
  char *mime = gbp_plugin_get_mime_types_description();
  if (mime == NULL) {
    gbp_plugin_add_mime_type ("application/x-gbp");
    mime = gbp_plugin_get_mime_types_description ();
  }

  return mime;
}

static NPError
fill_plugin_vtable(NPPluginFuncs *plugin_vtable)
{
  if (plugin_vtable == NULL)
    return NPERR_INVALID_FUNCTABLE_ERROR;

  if (plugin_vtable->size < sizeof (NPPluginFuncs))
		return NPERR_INVALID_FUNCTABLE_ERROR;
	
  plugin_vtable->size = sizeof (NPPluginFuncs);
	plugin_vtable->version = (NP_VERSION_MAJOR << 8) + NP_VERSION_MINOR;
	plugin_vtable->newp = NPP_New;
	plugin_vtable->destroy = NPP_Destroy;
	plugin_vtable->setwindow = NPP_SetWindow;
	plugin_vtable->newstream = NPP_NewStream;
	plugin_vtable->destroystream = NPP_DestroyStream;
	plugin_vtable->asfile = NPP_StreamAsFile;
	plugin_vtable->writeready = NPP_WriteReady;
	plugin_vtable->write = NPP_Write;
	plugin_vtable->print = NPP_Print;
	plugin_vtable->event = NPP_HandleEvent;
	plugin_vtable->urlnotify = NPP_URLNotify;
	plugin_vtable->javaClass = NULL;
	plugin_vtable->getvalue = NPP_GetValue;
	plugin_vtable->setvalue = NPP_SetValue;

  return NPERR_NO_ERROR;
}

NPError
NP_Initialize (NPNetscapeFuncs *mozilla_vtable, NPPluginFuncs *plugin_vtable)
{
	if (mozilla_vtable == NULL)
		return NPERR_INVALID_FUNCTABLE_ERROR;

#if 0
  if (mozilla_vtable->size < sizeof (NPNetscapeFuncs))
		return NPERR_INVALID_FUNCTABLE_ERROR;
#endif

  memcpy (&NPNFuncs, mozilla_vtable, mozilla_vtable->size);
  NPNFuncs.size = mozilla_vtable->size;

  /* initialize the NPClass used for the npruntime js object */
  gbp_np_class_init ();

#ifndef XP_MACOSX
  return fill_plugin_vtable (plugin_vtable);
#endif

  return NPERR_NO_ERROR;
}

NPError
NP_GetEntryPoints(NPPluginFuncs *plugin_vtable)
{
  return fill_plugin_vtable (plugin_vtable);
}

NPError
NP_Shutdown ()
{
  GSList *walk;
  gbp_np_class_free ();

  g_static_mutex_lock (&pending_invoke_data_lock);
  for (walk = pending_invoke_data; walk != NULL; walk = walk->next)
    invoke_data_free ((InvokeData *) walk->data, FALSE);
  g_slist_free (pending_invoke_data);
  pending_invoke_data = NULL;
  g_static_mutex_unlock (&pending_invoke_data_lock);

  return NPERR_NO_ERROR;
}

NPError NP_GetValue (NPP instance, NPPVariable variable, void *value)
{
  NPError rv = NPERR_NO_ERROR;

  switch (variable) {
    case NPPVpluginNameString:
      *((const char **) value) = "GStreamer Browser Plugin";
      break;
    case NPPVpluginDescriptionString:
      *((const char **) value) = "GStreamer based playback plugin";
      break;
    case NPPVpluginNeedsXEmbed:
      *((NPBool *) value) = TRUE;
      break;
    case NPPVpluginScriptableIID:
    case NPPVpluginScriptableInstance:
      rv = NPERR_GENERIC_ERROR;
      break;
    case NPPVpluginScriptableNPObject:
      *((NPObject **) value) = NPN_CreateObject (instance,
          (NPClass *) &gbp_np_class);
      break;
    default:
      rv = NPERR_GENERIC_ERROR;
      break;
  }

  return rv;
}

NPError NP_SetValue (NPP instance, NPNVariable variable, void *ret_value)
{
  return NPERR_GENERIC_ERROR;
}

InvokeData *
invoke_data_new (NPP instance, NPObject *object, int n_args)
{
  InvokeData *invoke_data = (InvokeData *) NPN_MemAlloc (sizeof (InvokeData));

  invoke_data->instance = instance;
  invoke_data->object = NPN_RetainObject (object);
  invoke_data->args = NPN_MemAlloc (sizeof (NPVariant) * n_args);
  invoke_data->n_args = n_args;

  g_static_mutex_lock (&pending_invoke_data_lock);
  pending_invoke_data = g_slist_append (pending_invoke_data, invoke_data);
  g_static_mutex_unlock (&pending_invoke_data_lock);

  return invoke_data;
}

void
invoke_data_free (InvokeData *invoke_data, gboolean remove_from_pending_slist)
{
#if 0
  int i;
#endif

  /* FIXME: This calls PR_Free (jemalloc) and segfaults. Odd since
   * NPN_MemAlloc does call PR_Malloc. Weird. */

#if 0
  for (i = 0; i < invoke_data->n_args; ++i ) {
    NPN_ReleaseVariantValue (&invoke_data->args[i]);
  }
#endif

  NPN_ReleaseObject (invoke_data->object);

  NPN_MemFree (invoke_data->args);

  if (remove_from_pending_slist) {
    g_static_mutex_lock (&pending_invoke_data_lock);
    pending_invoke_data = g_slist_remove (pending_invoke_data, invoke_data);
    g_static_mutex_unlock (&pending_invoke_data_lock);
  }

  NPN_MemFree (invoke_data);
}

void invoke_data_cb (void *user_data)
{
  NPVariant result;
  InvokeData *invoke_data = (InvokeData *) user_data;

  NPN_InvokeDefault (invoke_data->instance, invoke_data->object,
      invoke_data->args, invoke_data->n_args, &result);

  /* just ignore the return value for now */
  NPN_ReleaseVariantValue (&result);

  invoke_data_free (invoke_data, TRUE);
}

void on_error_cb (GbpPlayer *player, GError *error, const char *debug,
    gpointer user_data)
{
  NPP instance = (NPP) user_data;
  NPPGbpData *data = (NPPGbpData *) instance->pdata;
  char *debug_copy;
  char *message_copy;
  InvokeData *invoke_data;

  g_return_if_fail (player != NULL);
  g_return_if_fail (error != NULL);

  g_printerr ("error %s: %s\n", error->message, debug);

  if (data->errorHandler == NULL)
    return;

  invoke_data = invoke_data_new (instance, data->errorHandler, 2);

  /* copy message and debug as they will be freed once we return */
  message_copy = NPN_MemAlloc (strlen (error->message) + 1);
  strcpy (message_copy, error->message);
  debug_copy = NPN_MemAlloc (strlen (debug) + 1);
  strcpy (debug_copy, debug);

  STRINGZ_TO_NPVARIANT (message_copy, invoke_data->args[0]);
  STRINGZ_TO_NPVARIANT (debug_copy, invoke_data->args[1]);

  NPN_PluginThreadAsyncCall (instance, invoke_data_cb, invoke_data);
}

void on_state_cb (GbpPlayer *player, gpointer user_data)
{
  StateClosure *state_closure = (StateClosure *) user_data;
  NPP instance = state_closure->instance;
  NPPGbpData *data = (NPPGbpData *) instance->pdata;
  InvokeData *invoke_data;

  g_return_if_fail (player != NULL);

  g_print ("new state %s\n", state_closure->state);

  if (data->stateHandler == NULL)
    return;

  invoke_data = invoke_data_new (instance, data->stateHandler, 1);

  STRINGZ_TO_NPVARIANT (state_closure->state, invoke_data->args[0]);

  NPN_PluginThreadAsyncCall (instance, invoke_data_cb, invoke_data);
}

