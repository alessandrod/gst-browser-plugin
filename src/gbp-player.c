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

#include "gbp-player.h"
#include "gbp-marshal.h"

GST_DEBUG_CATEGORY (gbp_player_debug);

G_DEFINE_TYPE (GbpPlayer, gbp_player, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_URI,
  PROP_XID,
  PROP_WIDTH,
  PROP_HEIGHT
};

enum {
  SIGNAL_PLAYING,
  SIGNAL_PAUSED,
  SIGNAL_STOPPED,
  SIGNAL_ERROR,
  LAST_SIGNAL
};

struct _GbpPlayerPrivate
{
  gulong xid;
  char *uri;
  gboolean uri_changed;
  guint width;
  guint height;
  gboolean have_pipeline;
  GstPipeline *pipeline;
  GstBus *bus;
  gboolean disposed;
};

static guint player_signals[LAST_SIGNAL];

static void gbp_player_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gbp_player_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void on_bus_state_changed_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player);
static void on_bus_eos_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player);
static void on_bus_error_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player);
static void
gbp_player_dispose (GObject *object)
{
  GbpPlayer *player = GBP_PLAYER (object);

  if (!player->priv->disposed) {
    g_object_unref (player->priv->pipeline);
    g_object_unref (player->priv->bus);
    player->priv->disposed = TRUE;
  }

  G_OBJECT_CLASS (gbp_player_parent_class)->dispose (object);
}

static void
gbp_player_finalize (GObject *object)
{
  GbpPlayer *player = GBP_PLAYER (object);
 
  g_free (player->priv->uri);

  G_OBJECT_CLASS (gbp_player_parent_class)->finalize (object);
}

static void
gbp_player_class_init (GbpPlayerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gbp_player_get_property;
  gobject_class->set_property = gbp_player_set_property;
  gobject_class->dispose = gbp_player_dispose;
  gobject_class->finalize = gbp_player_finalize;

  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "Uri", "Playback URI",
          "", G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_XID,
      g_param_spec_ulong ("xid", "XID", "Window XID",
          0, G_MAXULONG, 0, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_uint ("width", "Width", "Width",
          0, G_MAXINT32, 0, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_uint ("height", "Height", "Height",
          0, G_MAXINT32, 0, G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  player_signals[SIGNAL_PLAYING] = g_signal_new ("playing",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GbpPlayerClass, playing), NULL, NULL,
      gbp_marshal_VOID__VOID, G_TYPE_NONE, 0);
  
  player_signals[SIGNAL_PAUSED] = g_signal_new ("paused",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GbpPlayerClass, paused), NULL, NULL,
      gbp_marshal_VOID__VOID, G_TYPE_NONE, 0);
  
  player_signals[SIGNAL_STOPPED] = g_signal_new ("stopped",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GbpPlayerClass, stopped), NULL, NULL,
      gbp_marshal_VOID__VOID, G_TYPE_NONE, 0);
  
  player_signals[SIGNAL_ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GbpPlayerClass, error), NULL, NULL,
      gbp_marshal_VOID__POINTER_STRING, G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_STRING);

  g_type_class_add_private (klass, sizeof (GbpPlayerPrivate));
}

static void
gbp_player_init (GbpPlayer *player)
{
  GbpPlayerPrivate *priv;
  
  player->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (player,
      GBP_TYPE_PLAYER, GbpPlayerPrivate);
}

static void
gbp_player_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GbpPlayer *player = GBP_PLAYER (object);
  
  switch (prop_id)
  {
    case PROP_URI:
      g_value_set_string (value, player->priv->uri);
      break;
    case PROP_XID:
      g_value_set_ulong (value, player->priv->xid);
      break;
    case PROP_WIDTH:
      g_value_set_uint (value, player->priv->width);
      break;
    case PROP_HEIGHT:
      g_value_set_uint (value, player->priv->height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gbp_player_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GbpPlayer *player = GBP_PLAYER (object);

  switch (prop_id)
  {
    case PROP_URI:
      player->priv->uri = g_value_dup_string (value);
      player->priv->uri_changed = TRUE;
      break;
    case PROP_XID:
      player->priv->xid = g_value_get_ulong (value);
      break;
    case PROP_WIDTH:
      player->priv->width = g_value_get_uint (value);
      break;
    case PROP_HEIGHT:
      player->priv->height = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  if (prop_id == PROP_WIDTH || prop_id == PROP_HEIGHT)
    /* FIXME */
    ;
}

static gboolean
build_pipeline (GbpPlayer *player)
{
  player->priv->pipeline = GST_PIPELINE (gst_element_factory_make ("playbin2", NULL));
  if (player->priv->pipeline == NULL) {
    /* FIXME: create our domain */
    GError *error = g_error_new (GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_FAILED, "couldn't find playbin");
    
    g_signal_emit (player, player_signals[SIGNAL_ERROR], 0,
        error, "more debug than that?");
    
    g_error_free (error);
    return FALSE;
  }   

  player->priv->bus = gst_pipeline_get_bus (player->priv->pipeline);
  gst_bus_enable_sync_message_emission (player->priv->bus);
  g_object_connect (player->priv->bus,
      "signal::sync-message::state-changed", G_CALLBACK (on_bus_state_changed_cb), player,
      "signal::sync-message::eos", G_CALLBACK (on_bus_eos_cb), player,
      "signal::sync-message::error", G_CALLBACK (on_bus_error_cb), player,
      NULL);

  player->priv->have_pipeline = TRUE;
  return TRUE;
}

void
gbp_player_start (GbpPlayer *player)
{
  g_return_if_fail (player != NULL);

  if (player->priv->have_pipeline == FALSE) {
    if (!build_pipeline (player))
      /* player::error has been emitted, return */
      return;
  }

  if (player->priv->uri_changed) {
    g_object_set (player->priv->pipeline, "uri", player->priv->uri, NULL);
    player->priv->uri_changed = FALSE;
  }

  gst_element_set_state (GST_ELEMENT (player->priv->pipeline),
      GST_STATE_PLAYING);
}

void
gbp_player_pause (GbpPlayer *player)
{
  g_return_if_fail (player != NULL);
  
  gst_element_set_state (GST_ELEMENT (player->priv->pipeline),
      GST_STATE_PAUSED);
}

void
gbp_player_stop (GbpPlayer *player)
{
  g_return_if_fail (player != NULL);
  
  gst_element_set_state (GST_ELEMENT (player->priv->pipeline),
      GST_STATE_NULL);
}

static void
on_bus_state_changed_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player)
{
  GstState old_state, new_state, pending_state;

  if (message->src != GST_OBJECT (player->priv->pipeline))
    return;

  gst_message_parse_state_changed (message,
      &old_state, &new_state, &pending_state);

  if (new_state == GST_STATE_PAUSED)
    /* FIXME: this will also happen during buffering */
    g_signal_emit (player, player_signals[SIGNAL_PAUSED], 0);
}

static void
on_bus_eos_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player)
{
  g_signal_emit (player, player_signals[SIGNAL_STOPPED], 0);
}

static void
on_bus_error_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player)
{
  GError *error;
  char *debug;

  gst_message_parse_error (message, &error, &debug);

  g_signal_emit (player, player_signals[SIGNAL_ERROR], 0, error, debug);
}
