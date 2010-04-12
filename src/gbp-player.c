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

#include <string.h>
#include <gst/interfaces/xoverlay.h>
#include "gbp-player.h"
#include "gbp-marshal.h"

GST_DEBUG_CATEGORY (gbp_player_debug);

G_DEFINE_TYPE (GbpPlayer, gbp_player, GST_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_URI,
  PROP_XID,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_VOLUME,
  PROP_HAVE_AUDIO
};

enum {
  SIGNAL_PLAYING,
  SIGNAL_PAUSED,
  SIGNAL_STOPPED,
  SIGNAL_EOS,
  SIGNAL_ERROR,
#ifdef XP_MACOSX
  SIGNAL_NSVIEW_READY,
#endif
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
  GstClockTime latency;
  GstClockTime tcp_timeout;
  gboolean disposed;
  gboolean reset_state;
  gdouble volume;
  gboolean have_audio;
};

static guint player_signals[LAST_SIGNAL];

static void gbp_player_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gbp_player_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void playbin_source_cb (GstElement *playbin,
    GParamSpec *pspec, GbpPlayer *player);
static void autovideosink_element_added_cb (GstElement *autovideosink,
    GstElement *element, GbpPlayer *player);
static void on_bus_state_changed_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player);
static void on_bus_eos_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player);
static void on_bus_error_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player);
static void on_bus_element_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player);

static void
gbp_player_dispose (GObject *object)
{
  GbpPlayer *player = GBP_PLAYER (object);

  if (!player->priv->disposed) {
    player->priv->disposed = TRUE;
    if (player->priv->pipeline != NULL) {
      g_object_unref (player->priv->pipeline);
      g_object_unref (player->priv->bus);

      player->priv->pipeline = NULL;
      player->priv->bus = NULL;
    }
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
  GParamFlags flags = (GParamFlags) (G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  gobject_class->get_property = gbp_player_get_property;
  gobject_class->set_property = gbp_player_set_property;
  gobject_class->dispose = gbp_player_dispose;
  gobject_class->finalize = gbp_player_finalize;

  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "Uri", "Playback URI",
          "", flags));

  g_object_class_install_property (gobject_class, PROP_XID,
      g_param_spec_ulong ("xid", "XID", "Window XID",
          0, G_MAXULONG, 0, flags));

  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_uint ("width", "Width", "Width",
          0, G_MAXINT32, 0, flags));

  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_uint ("height", "Height", "Height",
          0, G_MAXINT32, 0, flags));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume",
          0.0, G_MAXDOUBLE, 1.0, flags));
  
  g_object_class_install_property (gobject_class, PROP_HAVE_AUDIO,
      g_param_spec_boolean ("have-audio", "Have Audio", "Have Audio",
          TRUE, flags));

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

  player_signals[SIGNAL_EOS] = g_signal_new ("eos",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GbpPlayerClass, eos), NULL, NULL,
      gbp_marshal_VOID__VOID, G_TYPE_NONE, 0);

  player_signals[SIGNAL_ERROR] = g_signal_new ("error",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GbpPlayerClass, error), NULL, NULL,
      gbp_marshal_VOID__POINTER_STRING, G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_STRING);

#ifdef XP_MACOSX
  player_signals[SIGNAL_NSVIEW_READY] = g_signal_new ("nsview-ready",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GbpPlayerClass, nsview_ready), NULL, NULL,
      gbp_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
#endif

  g_type_class_add_private (klass, sizeof (GbpPlayerPrivate));
}

static void
gbp_player_init (GbpPlayer *player)
{
  GbpPlayerPrivate *priv;

  player->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (player,
      GBP_TYPE_PLAYER, GbpPlayerPrivate);
  player->priv->latency = 300 * GST_MSECOND;
  player->priv->tcp_timeout = 5 * GST_SECOND;
  player->priv->have_audio = TRUE;
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
    case PROP_VOLUME:
      g_value_set_double (value, player->priv->volume);
      break;
    case PROP_HAVE_AUDIO:
      g_value_set_boolean (value, player->priv->have_audio);
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
    case PROP_VOLUME:
      player->priv->volume = g_value_get_double (value);

      if (player->priv->have_pipeline)
        g_object_set (player->priv->pipeline, "volume",
            g_value_get_double (value), NULL);

      break;
    case PROP_HAVE_AUDIO:
    {
      gboolean have_audio;

      have_audio = g_value_get_boolean (value);
      if (have_audio != player->priv->have_audio) {
        player->priv->have_audio = have_audio;
        player->priv->have_pipeline = FALSE;
      }

      break;
    }
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
  GstElement *autovideosink;
  GstElement *audiosink;

  if (player->priv->pipeline != NULL) {
    gst_element_set_state (GST_ELEMENT (player->priv->pipeline), GST_STATE_NULL);
    g_object_unref (player->priv->pipeline);
  }

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

#ifdef XP_MACOSX
  autovideosink = gst_element_factory_make("osxvideosink", NULL);
#else
  autovideosink = gst_element_factory_make("autovideosink", NULL);
#endif
  if (autovideosink == NULL) {
    GError *error = g_error_new (GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_FAILED, "couldn't find autovideosink");

    g_signal_emit (player, player_signals[SIGNAL_ERROR], 0,
        error, "more debug than that?");

    g_error_free (error);

    g_object_unref (player->priv->pipeline);
    player->priv->pipeline = NULL;

    return FALSE;
  }

  if (player->priv->have_audio) {
    audiosink = gst_element_factory_make("autoaudiosink", NULL);
  } else {
    audiosink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (audiosink, "sync", TRUE, NULL);
  }

  if (audiosink == NULL) {
    GError *error = g_error_new (GST_LIBRARY_ERROR,
        GST_LIBRARY_ERROR_FAILED, "couldn't find %s",
            player->priv->have_audio ? "autoaudiosink" : "fakesink");

    g_signal_emit (player, player_signals[SIGNAL_ERROR], 0,
        error, "more debug than that?");

    g_error_free (error);

    g_object_unref (player->priv->pipeline);
    player->priv->pipeline = NULL;

    return FALSE;
  }

  g_object_set (G_OBJECT (player->priv->pipeline), "video-sink", autovideosink, NULL);
  g_object_set (G_OBJECT (player->priv->pipeline), "audio-sink", audiosink, NULL);
  
  player->priv->bus = gst_pipeline_get_bus (player->priv->pipeline);
  gst_bus_enable_sync_message_emission (player->priv->bus);
  g_object_connect (player->priv->bus,
      "signal::sync-message::state-changed", G_CALLBACK (on_bus_state_changed_cb), player,
      "signal::sync-message::eos", G_CALLBACK (on_bus_eos_cb), player,
      "signal::sync-message::error", G_CALLBACK (on_bus_error_cb), player,
      "signal::sync-message::element", G_CALLBACK (on_bus_element_cb), player,
      NULL);

  g_object_connect (player->priv->pipeline,
      "signal::notify::source", playbin_source_cb, player,
      NULL);

  g_object_connect (autovideosink,
      "signal::element-added", autovideosink_element_added_cb, player,
      NULL);

  g_object_set (player->priv->pipeline,
      "volume", player->priv->volume, NULL);

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
    gbp_player_stop (player);

    g_object_set (player->priv->pipeline, "uri", player->priv->uri, NULL);
    player->priv->uri_changed = FALSE;
  }

  if (player->priv->reset_state) {
    gbp_player_stop (player);
    player->priv->reset_state = FALSE;
  }

  gst_element_set_state (GST_ELEMENT (player->priv->pipeline),
      GST_STATE_PLAYING);
}

void
gbp_player_pause (GbpPlayer *player)
{
  g_return_if_fail (player != NULL);

  if (player->priv->have_pipeline == FALSE) {
    if (!build_pipeline (player))
      /* player::error has been emitted, return */
      return;
  }

  if (player->priv->uri_changed) {
    gbp_player_stop (player);

    g_object_set (player->priv->pipeline, "uri", player->priv->uri, NULL);
    player->priv->uri_changed = FALSE;
  }

  if (player->priv->reset_state) {
    gbp_player_stop (player);
    player->priv->reset_state = FALSE;
  }

  gst_element_set_state (GST_ELEMENT (player->priv->pipeline),
      GST_STATE_PAUSED);
}

GstClockTime
gbp_player_get_duration (GbpPlayer *player)
{
  gint64 duration;
  GstFormat format = GST_FORMAT_TIME;

  g_return_val_if_fail (player != NULL, GST_CLOCK_TIME_NONE);

  if (!player->priv->have_pipeline)
    return GST_CLOCK_TIME_NONE;

  if (!gst_element_query_duration (GST_ELEMENT (player->priv->pipeline), &format, &duration))
    return GST_CLOCK_TIME_NONE;

  return (GstClockTime) duration;
}

GstClockTime
gbp_player_get_position (GbpPlayer *player)
{
  gint64 position;
  GstFormat format = GST_FORMAT_TIME;

  g_return_val_if_fail (player != NULL, GST_CLOCK_TIME_NONE);

  if (!player->priv->have_pipeline)
    return GST_CLOCK_TIME_NONE;

  if (!gst_element_query_position (GST_ELEMENT (player->priv->pipeline), &format, &position))
    return GST_CLOCK_TIME_NONE;

  return (GstClockTime) position;
}

gboolean
gbp_player_seek (GbpPlayer *player, GstClockTime position, gdouble rate)
{
  GstFormat format;
  GstSeekFlags seek_flags;
  guint64 start, stop;

  g_return_val_if_fail (player != NULL, FALSE);

  if (!player->priv->have_pipeline)
    return FALSE;

  format = GST_FORMAT_TIME;
  seek_flags = GST_SEEK_FLAG_FLUSH;

  if (rate > 0) {
    start = position;
    stop = -1;
  } else {
    start = -1;
    stop = position;
  }

  return gst_element_seek (GST_ELEMENT (player->priv->pipeline), rate,
      format, seek_flags, GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_SET, stop);
}

void
gbp_player_stop (GbpPlayer *player)
{
  g_return_if_fail (player != NULL);

  if (player->priv->pipeline == NULL)
    return;

  gst_element_set_state (GST_ELEMENT (player->priv->pipeline),
      GST_STATE_NULL);
  gst_element_get_state (GST_ELEMENT (player->priv->pipeline),
      NULL, NULL, GST_CLOCK_TIME_NONE);
}

static void
playbin_source_cb (GstElement *playbin,
    GParamSpec *pspec, GbpPlayer *player)
{
  GstElement *element;
  GObjectClass *klass;

  g_object_get (G_OBJECT (playbin), "source", &element, NULL);
  klass = G_OBJECT_GET_CLASS (element);

  if (g_object_class_find_property (klass, "latency")) {
    g_object_set (element, "latency",
        GST_TIME_AS_MSECONDS (player->priv->latency), NULL);
  }

  if (g_object_class_find_property (klass, "tcp-timeout")) {
    g_object_set (element, "tcp-timeout",
        GST_TIME_AS_USECONDS (player->priv->tcp_timeout), NULL);
  }
}

static void
autovideosink_element_added_cb (GstElement *autovideosink,
    GstElement *element, GbpPlayer *player)
{
  GObjectClass *klass;

  GST_INFO_OBJECT (player, "using sink %s", gst_element_get_name (element));

  klass = G_OBJECT_GET_CLASS (element);
  if (!g_object_class_find_property (klass, "double-buffer"))
    return;
  
  g_object_set (G_OBJECT (element), "double-buffer", FALSE, NULL);
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

  if (new_state == GST_STATE_READY && old_state > GST_STATE_READY &&
      pending_state <= GST_STATE_READY) {
    g_signal_emit (player, player_signals[SIGNAL_STOPPED], 0);
  } else if (new_state == GST_STATE_PAUSED &&
        pending_state == GST_STATE_VOID_PENDING) {
    g_signal_emit (player, player_signals[SIGNAL_PAUSED], 0);
  } else if (new_state == GST_STATE_PLAYING &&
      pending_state == GST_STATE_VOID_PENDING) {
    g_signal_emit (player, player_signals[SIGNAL_PLAYING], 0);
  }
}

static void
on_bus_eos_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player)
{
  player->priv->reset_state = TRUE;
  g_signal_emit (player, player_signals[SIGNAL_EOS], 0);
}

static void
on_bus_error_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player)
{
  GError *error;
  char *debug;

  gst_message_parse_error (message, &error, &debug);

  player->priv->reset_state = TRUE;
  g_signal_emit (player, player_signals[SIGNAL_ERROR], 0, error, debug);
}

static void
on_bus_element_cb (GstBus *bus, GstMessage *message,
    GbpPlayer *player)
{
  const GstStructure *structure;
  const gchar *structure_name;
  GstElement *sink;
#ifdef XP_MACOSX
  void *nsview;
#endif
  structure = gst_message_get_structure (message);
  if (structure == NULL)
    return;

  structure_name = gst_structure_get_name (structure);

#ifdef XP_MACOSX
  if (!strcmp (structure_name, "have-ns-view")) {
    nsview = g_value_get_pointer(gst_structure_get_value(
        gst_message_get_structure(message), "nsview"));
    g_signal_emit (player, player_signals[SIGNAL_NSVIEW_READY], 0,
        nsview);
  }
  return;
#endif


  if (!strcmp (structure_name, "prepare-xwindow-id")) {
    sink = GST_ELEMENT (message->src);
    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (sink), player->priv->xid);
  }
}
