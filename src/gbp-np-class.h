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

#ifndef GBP_NP_CLASS_H
#define GBP_NP_CLASS_H

#include "gbp-npapi.h"
#include "npruntime.h"

G_BEGIN_DECLS

typedef struct _GbpNPClass
{
  NPClass klass;
  GThread *playback_thread;
  GAsyncQueue *playback_queue;
} GbpNPClass;

typedef struct _GbpNPObject
{
  NPObject object;
  NPP instance;

} GbpNPObject;

extern GbpNPClass gbp_np_class;

void gbp_np_class_init ();
void gbp_np_class_free ();
void gbp_np_class_start_playback_thread();
void gbp_np_class_stop_playback_thread();

G_END_DECLS

#endif /* GBP_NP_CLASS_H */
