/*
 * Mex - a media explorer
 *
 * Copyright © 2011 Intel Corporation.
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

#ifndef _MEX_TRACKER_CONTENT_H
#define _MEX_TRACKER_CONTENT_H

#include <mex-generic-content.h>

G_BEGIN_DECLS

#define MEX_TYPE_TRACKER_CONTENT mex_tracker_content_get_type()

#define MEX_TRACKER_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  MEX_TYPE_TRACKER_CONTENT, MexTrackerContent))

#define MEX_TRACKER_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  MEX_TYPE_TRACKER_CONTENT, MexTrackerContentClass))

#define MEX_IS_TRACKER_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  MEX_TYPE_TRACKER_CONTENT))

#define MEX_IS_TRACKER_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  MEX_TYPE_TRACKER_CONTENT))

#define MEX_TRACKER_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  MEX_TYPE_TRACKER_CONTENT, MexTrackerContentClass))

typedef struct _MexTrackerContent MexTrackerContent;
typedef struct _MexTrackerContentClass MexTrackerContentClass;
typedef struct _MexTrackerContentPrivate MexTrackerContentPrivate;

struct _MexTrackerContent
{
  MexGenericContent parent;

  MexTrackerContentPrivate *priv;
};

struct _MexTrackerContentClass
{
  MexGenericContentClass parent_class;
};

GType mex_tracker_content_get_type (void) G_GNUC_CONST;

MexContent *mex_tracker_content_new (void);

void mex_tracker_content_set_in_setup (MexTrackerContent *content, gboolean in_setup);

gboolean mex_tracker_content_get_in_setup (MexTrackerContent *content);

G_END_DECLS

#endif /* _MEX_TRACKER_CONTENT_H */
