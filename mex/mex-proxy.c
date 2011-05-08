/*
 * Mex - a media explorer
 *
 * Copyright © 2010, 2011 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>
 */

/**
 * SECTION:mex-proxy
 * @short_description: An abstract proxy for mapping #MexModel
 * content items to arbitrary #GObjects.
 *
 * #MexProxy can also be used to limit the number of content items
 * in the model which are translated to GObjects (see mex_proxy_set_limit()),
 * and reorder the content in a model (by changing the start point from
 * which items are added: see mex_proxy_start_at()).
 */

#include "mex-proxy.h"
#include "mex-marshal.h"
#include "mex-content.h"
#include <clutter/clutter.h>

G_DEFINE_ABSTRACT_TYPE (MexProxy, mex_proxy, G_TYPE_OBJECT)

#define PROXY_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MEX_TYPE_PROXY, MexProxyPrivate))

enum
{
  PROP_0,

  PROP_MODEL,
  PROP_TYPE,
};

enum
{
  OBJECT_CREATED,
  OBJECT_REMOVED,

  LAST_SIGNAL
};

struct _MexProxyPrivate
{
  MexModel   *model;
  GType       object_type;
  GHashTable *content_to_object;

  GQueue     *to_add;
  GHashTable *to_add_hash;
  GTimer     *timer;
  guint       timer_timeout;
};

static guint signals[LAST_SIGNAL] = { 0, };

static void mex_proxy_set_model (MexProxy *proxy, MexModel *model);
static void mex_proxy_object_gone_cb (MexProxy *proxy, GObject *object);
static void mex_proxy_controller_changed_cb (GController          *controller,
                                             GControllerAction     action,
                                             GControllerReference *ref,
                                             MexProxy             *proxy);

static void
mex_proxy_get_property (GObject    *object,
                        guint       property_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  MexProxy *proxy = MEX_PROXY (object);

  switch (property_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, mex_proxy_get_model (proxy));
      break;

    case PROP_TYPE:
      g_value_set_gtype (value, mex_proxy_get_object_type (proxy));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mex_proxy_set_property (GObject      *object,
                        guint         property_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  MexProxy *proxy = MEX_PROXY (object);
  MexProxyPrivate *priv = proxy->priv;

  switch (property_id)
    {
    case PROP_MODEL:
      mex_proxy_set_model (proxy, g_value_get_object (value));
      break;

    case PROP_TYPE:
      priv->object_type = g_value_get_gtype (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mex_proxy_dispose (GObject *object)
{
  MexProxy *proxy = (MexProxy *) object;
  MexProxyPrivate *priv = proxy->priv;

  if (priv->model)
    {
      mex_proxy_set_model (proxy, NULL);
    }

  if (priv->content_to_object)
    {
      g_hash_table_unref (priv->content_to_object);
      priv->content_to_object = NULL;
    }

  if (priv->to_add)
    {
      g_queue_free (priv->to_add);
      priv->to_add = NULL;
    }

  if (priv->to_add_hash)
    {
      g_hash_table_unref (priv->to_add_hash);
      priv->to_add_hash = NULL;
    }

  if (priv->timer)
    {
      g_timer_destroy (priv->timer);
      priv->timer = NULL;
    }

  G_OBJECT_CLASS (mex_proxy_parent_class)->dispose (object);
}

static void
mex_proxy_finalize (GObject *object)
{
  G_OBJECT_CLASS (mex_proxy_parent_class)->finalize (object);
}

static void
mex_proxy_class_init (MexProxyClass *klass)
{
  GParamSpec *pspec;

  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MexProxyPrivate));

  object_class->get_property = mex_proxy_get_property;
  object_class->set_property = mex_proxy_set_property;
  object_class->dispose = mex_proxy_dispose;
  object_class->finalize = mex_proxy_finalize;

  pspec = g_param_spec_object ("model",
                               "Model",
                               "MexModel the proxy is listening to.",
                               G_TYPE_OBJECT,
                               G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MODEL, pspec);

  pspec = g_param_spec_gtype ("object-type",
                              "Object type",
                              "GType for creating GObjects.",
                              G_TYPE_OBJECT,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                              G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TYPE, pspec);

  signals[OBJECT_CREATED] =
    g_signal_new ("object-created",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MexProxyClass,
                                   object_created),
                  NULL, NULL,
                  mex_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_OBJECT);

  signals[OBJECT_REMOVED] =
    g_signal_new ("object-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (MexProxyClass,
                                   object_removed),
                  NULL, NULL,
                  mex_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_OBJECT);
}

static void
mex_proxy_init (MexProxy *self)
{
  MexProxyPrivate *priv = self->priv = PROXY_PRIVATE (self);

  priv->content_to_object =
    g_hash_table_new_full (NULL,
                           NULL,
                           (GDestroyNotify)g_object_unref,
                           NULL);

  priv->timer = g_timer_new ();
  priv->to_add = g_queue_new ();
  priv->to_add_hash = g_hash_table_new (NULL, NULL);
}

MexModel *
mex_proxy_get_model (MexProxy *proxy)
{
  g_return_val_if_fail (MEX_IS_PROXY (proxy), NULL);
  return proxy->priv->model;
}

GType
mex_proxy_get_object_type (MexProxy *proxy)
{
  g_return_val_if_fail (MEX_IS_PROXY (proxy), G_TYPE_INVALID);
  return proxy->priv->object_type;
}

static void
mex_proxy_object_gone_cb (MexProxy *proxy,
                          GObject  *object)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, proxy->priv->content_to_object);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if ((GObject *)value == object)
        {
          g_hash_table_iter_remove (&iter);

          return;
        }
    }
}

static void
mex_proxy_add_content_no_defer (MexProxy   *proxy,
                                MexContent *content)
{
  GObject *object;

  MexProxyPrivate *priv = proxy->priv;

  object = g_object_new (priv->object_type, NULL);

  g_hash_table_insert (priv->content_to_object, g_object_ref_sink (content), object);

  g_object_weak_ref (object, (GWeakNotify)mex_proxy_object_gone_cb, proxy);

  g_object_ref_sink (object);
  g_signal_emit (proxy, signals[OBJECT_CREATED], 0, content, object);
  g_object_unref (object);
}

static gboolean
mex_proxy_stop_timer_cb (MexProxy *proxy)
{
  MexProxyPrivate *priv = proxy->priv;

  g_timer_start (priv->timer);

  while (g_timer_elapsed (priv->timer, NULL) * 1000 < 5 &&
         !g_queue_is_empty (priv->to_add))
    {
      MexContent *content = g_queue_pop_head (priv->to_add);
      g_hash_table_remove (priv->to_add_hash, content);

      mex_proxy_add_content_no_defer (proxy, content);
      g_object_unref (content);
    }

  g_timer_stop (priv->timer);

  if (g_queue_is_empty (priv->to_add))
    {
      priv->timer_timeout = 0;
      return FALSE;
    }
  else
    return TRUE;
}

static void
mex_proxy_add_content (MexProxy   *proxy,
                       MexContent *content)
{
  MexProxyPrivate *priv = proxy->priv;

  /* Start the timer or re-start its time-out */
  if (!priv->timer_timeout)
    {
      g_timer_start (priv->timer);
      priv->timer_timeout =
        g_idle_add_full (CLUTTER_PRIORITY_REDRAW,
                         (GSourceFunc)mex_proxy_stop_timer_cb,
                         proxy,
                         NULL);
    }

  /* Don't spend more than 5ms before hitting the main-loop again
   * creating/adding objects, and make sure to maintain order.
   */
  if (!g_queue_is_empty (priv->to_add) ||
      g_timer_elapsed (priv->timer, NULL) * 1000 >= 5)
    {
      g_queue_push_tail (priv->to_add, g_object_ref_sink (content));
      g_hash_table_insert (priv->to_add_hash, content,
                           g_queue_peek_tail_link (priv->to_add));
      return;
    }

  mex_proxy_add_content_no_defer (proxy, content);
}

static void
mex_proxy_remove_content (MexProxy   *proxy,
                          MexContent *content)
{
  GObject *object;
  GList *to_add_link;

  MexProxyPrivate *priv = proxy->priv;

  object = g_hash_table_lookup (priv->content_to_object, content);
  if (object)
    {
      g_object_ref (object);

      g_signal_emit (proxy, signals[OBJECT_REMOVED], 0, content, object);

      g_object_weak_unref (object,
                           (GWeakNotify)mex_proxy_object_gone_cb,
                           proxy);
      g_hash_table_remove (priv->content_to_object, content);

      g_object_unref (object);
    }
  else if ((to_add_link = g_hash_table_lookup (priv->to_add_hash, content)))
    {
      g_queue_delete_link (priv->to_add, to_add_link);
      g_hash_table_remove (priv->to_add_hash, content);
      g_object_unref (content);
    }
}

static void
mex_proxy_clear (MexProxy *proxy)
{
  MexProxyPrivate *priv = proxy->priv;
  GList *p, *contents = g_hash_table_get_keys (priv->content_to_object);

  for (p = contents; p; p = p->next)
    {
      MexContent *content = p->data;
      mex_proxy_remove_content (proxy, content);
    }

  g_queue_foreach (priv->to_add, (GFunc)g_object_unref, NULL);
  g_queue_clear (priv->to_add);
  g_hash_table_remove_all (priv->to_add_hash);

  g_list_free (contents);
}

static void
mex_proxy_set_model (MexProxy *proxy, MexModel *model)
{
  MexProxyPrivate *priv = proxy->priv;
  GController *controller;

  if (priv->model)
    {
      if (priv->timer_timeout)
        {
          g_source_remove (priv->timer_timeout);
          priv->timer_timeout = 0;
        }

      controller = mex_model_get_controller (priv->model);

      g_signal_handlers_disconnect_by_func (controller,
                                            mex_proxy_controller_changed_cb,
                                            proxy);
      g_object_unref (controller);

      mex_proxy_clear (proxy);

      g_object_unref (priv->model);
    }

  priv->model = model;

  if (priv->model)
    {
      guint i, length;

      g_object_ref_sink (priv->model);

      length = mex_model_get_length (priv->model);
      for (i = 0; i < length; i++)
        {
          MexContent *content = mex_model_get_content (priv->model, i);
          mex_proxy_add_content (proxy, content);
        }

      controller = mex_model_get_controller (priv->model);
      g_signal_connect_after (controller, "changed",
                              G_CALLBACK (mex_proxy_controller_changed_cb),
                              proxy);

      g_object_unref (controller);
    }
}

static void
mex_proxy_controller_changed_cb (GController          *controller,
                                 GControllerAction     action,
                                 GControllerReference *ref,
                                 MexProxy             *proxy)
{
  gint i, n_indices;
  MexContent *content;

  MexProxyPrivate *priv = proxy->priv;

  n_indices = g_controller_reference_get_n_indices (ref);

  switch (action)
    {
    case G_CONTROLLER_ADD:
      for (i = 0; i < n_indices; i++)
        {
          gint content_index = g_controller_reference_get_index_uint (ref, i);
          content = mex_model_get_content (priv->model, content_index);
          mex_proxy_add_content (proxy, content);
        }
      break;

    case G_CONTROLLER_REMOVE:
      for (i = 0; i < n_indices; i++)
        {
          gint content_index = g_controller_reference_get_index_uint (ref, i);
          content = mex_model_get_content (priv->model, content_index);
          mex_proxy_remove_content (proxy, content);
        }
      break;

    case G_CONTROLLER_UPDATE:
      /* Should be no need for this, GBinding sorts it out for us :) */
      break;

    case G_CONTROLLER_CLEAR:
      mex_proxy_clear (proxy);
      break;

    case G_CONTROLLER_REPLACE:
      mex_proxy_clear (proxy);
      i = 0;
      while ((content = mex_model_get_content (priv->model, i++)))
        mex_proxy_add_content (proxy, content);
      break;

    case G_CONTROLLER_INVALID_ACTION:
      g_warning (G_STRLOC ": Proxy controller has issued an error");
      break;

    default:
      g_warning (G_STRLOC ": Unhandled action");
      break;
    }
}
