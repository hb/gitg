/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * gitg
 * Copyright (C)  2009 <>
 * 
 * gitg is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * gitg is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GITG_AUTHOR_H_
#define _GITG_AUTHOR_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GITG_TYPE_AUTHOR             (gitg_author_get_type ())
#define GITG_AUTHOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GITG_TYPE_AUTHOR, GitgAuthor))
#define GITG_AUTHOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GITG_TYPE_AUTHOR, GitgAuthorClass))
#define GITG_IS_AUTHOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GITG_TYPE_AUTHOR))
#define GITG_IS_AUTHOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GITG_TYPE_AUTHOR))
#define GITG_AUTHOR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GITG_TYPE_AUTHOR, GitgAuthorClass))

typedef struct _GitgAuthorClass GitgAuthorClass;
typedef struct _GitgAuthor GitgAuthor;
typedef struct _GitgAuthorPrivate GitgAuthorPrivate;

struct _GitgAuthorClass
{
	GObjectClass parent_class;
};

struct _GitgAuthor
{
	GObject parent_instance;
	
	GitgAuthorPrivate *priv;
};

GType gitg_author_get_type (void) G_GNUC_CONST;

void gitg_author_set_email(GitgAuthor *author, gchar const *email);
gchar const *gitg_author_get_email(GitgAuthor *author);

void gitg_author_set_name(GitgAuthor *author, gchar const *name);
gchar const *gitg_author_get_name(GitgAuthor *author);

G_END_DECLS

#endif /* _GITG_AUTHOR_H_ */
