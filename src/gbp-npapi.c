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


void on_error_cb (GbpPlayer *player, GError *error, const char *debug,
    gpointer user_data);

NPError NP_GetValue (NPP instance, NPPVariable variable, void *ret_value);
NPError NP_SetValue (NPP instance, NPNVariable variable, void *ret_value);

NPNetscapeFuncs NPNFuncs;

/* NPP vtable symbols */
NPError
NPP_New (NPMIMEType plugin_type, NPP instance, uint16 mode,
    int16 argc, char *argn[], char *argv[], NPSavedData *saved_data)
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

  g_object_connect (G_OBJECT (player), "signal::error",
      G_CALLBACK (on_error_cb), instance, NULL);

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
  gbp_player_pause (data->player);

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

int32
NPP_WriteReady (NPP instance, NPStream* stream)
{
  return G_MAXINT32;
}

int32
NPP_Write (NPP instance, NPStream* stream,
    int32 offset, int32 len, void* buffer)
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
  static char *mime = gbp_plugin_get_mime_types_description();
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
	plugin_vtable->newp = NewNPP_NewProc (NPP_New);
	plugin_vtable->destroy = NewNPP_DestroyProc (NPP_Destroy);
	plugin_vtable->setwindow = NewNPP_SetWindowProc (NPP_SetWindow);
	plugin_vtable->newstream = NewNPP_NewStreamProc (NPP_NewStream);
	plugin_vtable->destroystream = NewNPP_DestroyStreamProc (NPP_DestroyStream);
	plugin_vtable->asfile = NewNPP_StreamAsFileProc (NPP_StreamAsFile);
	plugin_vtable->writeready = NewNPP_WriteReadyProc (NPP_WriteReady);
	plugin_vtable->write = NewNPP_WriteProc (NPP_Write);
	plugin_vtable->print = NewNPP_PrintProc (NPP_Print);
	plugin_vtable->event = NewNPP_HandleEventProc (NPP_HandleEvent);
	plugin_vtable->urlnotify = NewNPP_URLNotifyProc (NPP_URLNotify);
	plugin_vtable->javaClass = NULL;
	plugin_vtable->getvalue = NewNPP_GetValueProc (NPP_GetValue);
	plugin_vtable->setvalue = NewNPP_SetValueProc (NPP_SetValue);

  return NPERR_NO_ERROR;
}

NPError
NP_Initialize (NPNetscapeFuncs *mozilla_vtable, NPPluginFuncs *plugin_vtable)
{
	if (mozilla_vtable == NULL)
		return NPERR_INVALID_FUNCTABLE_ERROR;
	
  if (mozilla_vtable->size < sizeof (NPNetscapeFuncs))
		return NPERR_INVALID_FUNCTABLE_ERROR;

  memcpy (&NPNFuncs, mozilla_vtable, sizeof (NPNetscapeFuncs));
  NPNFuncs.size = sizeof (NPNetscapeFuncs);

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
  gbp_np_class_free ();

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

void on_error_cb (GbpPlayer *player, GError *error, const char *debug,
    gpointer user_data)
{
  NPP instance = (NPP) user_data;
  NPPGbpData *data = (NPPGbpData *) instance->pdata;
  NPVariant args[2];
  NPVariant result;

  g_return_if_fail (player != NULL);
  g_return_if_fail (error != NULL);

  if (data->errorHandler == NULL) {
    /* no error handler installed */

    g_printerr ("error %s: %s\n", error->message, debug);
    return;
  }

  /* call errorHandler (message, debug) */
  STRINGZ_TO_NPVARIANT (error->message, args[0]);
  STRINGZ_TO_NPVARIANT (debug, args[1]);

  /* ta-daaaa */
  NPN_InvokeDefault (instance, data->errorHandler, args, 2, &result);

  /* just ignore the return value for now */
  NPN_ReleaseVariantValue (&result);
}

