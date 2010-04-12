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

#include <gst/gst.h>

#ifndef GBP_PLAYER_H
#define GBP_PLAYER_H

G_BEGIN_DECLS

#define GBP_TYPE_PLAYER \
  (gbp_player_get_type())
#define GBP_PLAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GBP_TYPE_PLAYER,GbpPlayer))
#define GBP_PLAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GBP_TYPE_PLAYER,GbpPlayerClass))
#define GBP_IS_PLAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GBP_TYPE_PLAYER))
#define GBP_IS_PLAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GBP_TYPE_PLAYER))

GST_DEBUG_CATEGORY_EXTERN (gbp_player_debug);
#define GST_CAT_DEFAULT gbp_player_debug

typedef struct _GbpPlayer GbpPlayer;
typedef struct _GbpPlayerPrivate GbpPlayerPrivate;
typedef struct _GbpPlayerClass GbpPlayerClass;

struct _GbpPlayer {
  GstObject object;

  GbpPlayerPrivate *priv;
};

struct _GbpPlayerClass {
  GstObjectClass object_class;

  /* signals */
  void (*playing)(GbpPlayer *player);
  void (*paused)(GbpPlayer *player);
  void (*stopped)(GbpPlayer *player);
  void (*eos)(GbpPlayer *player);
  void (*error)(GbpPlayer *player, GError *error, const char *debug);
#ifdef XP_MACOSX
void (*nsview_ready)(GbpPlayer *player, void * nsview);
#endif
};

GType gbp_player_get_type(void);
void gbp_player_start (GbpPlayer *player);
void gbp_player_pause (GbpPlayer *player);
void gbp_player_stop (GbpPlayer *player);
GstClockTime gbp_player_get_duration (GbpPlayer *player);
GstClockTime gbp_player_get_position (GbpPlayer *player);
gboolean gbp_player_seek (GbpPlayer *player,
    GstClockTime position, gdouble rate);

G_END_DECLS

#endif /* GBP_PLAYER_H */
