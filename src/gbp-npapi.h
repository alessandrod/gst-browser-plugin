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
#ifndef GBP_NPAPI_H
#define GBP_NPAPI_H

#include "npapi.h"
#include "npruntime.h"
#include "npfunctions.h"
#include "gbp-player.h"

G_BEGIN_DECLS

#define NPP_INSTANCE_GET_DATA(instance) ((NPPGbpData *)(instance->pdata))

typedef struct _NPPGbpData
{
  GbpPlayer *player;
  NPObject *errorHandler;
  NPObject *stateHandler;
#ifndef PLAYBACK_THREAD_SINGLE
  GThread *playback_thread;
  GAsyncQueue *playback_queue;
#endif
  char *state;
} NPPGbpData;

char *NP_GetMIMEDescription();
NPError NP_Initialize (NPNetscapeFuncs *mozilla_vtable, NPPluginFuncs *plugin_vtable);
NPError NP_GetEntryPoints (NPPluginFuncs *plugin_vtable);
NPError NP_Shutdown ();
NPError NP_GetValue (NPP instance, NPPVariable variable, void *value);
NPError NP_SetValue (NPP instance, NPNVariable variable, void *ret_value);
void npp_gbp_data_free (NPPGbpData *data);

G_END_DECLS

#endif /* GBP_NPAPI_H */
