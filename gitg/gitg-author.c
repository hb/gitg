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

#include "gitg-author.h"


/* Properties */
enum {
	PROP_0,

	PROP_NAME,
	PROP_EMAIL
};

struct _GitgAuthorPrivate
{
	gchar *name;
	gchar *email;
};

G_DEFINE_TYPE (GitgAuthor, gitg_author, G_TYPE_OBJECT);

static void
author_set_name (GitgAuthorPrivate *priv, const char *name)
{
	g_free (priv->name);

	priv->name = g_strdup (name);
}

static void
author_set_email (GitgAuthorPrivate *priv, const char *email)
{
}

static void
gitg_author_init (GitgAuthor *object)
{
	/* TODO: Add initialization code here */
}

static void
gitg_author_finalize (GObject *object)
{
	/* TODO: Add deinitalization code here */

	G_OBJECT_CLASS (gitg_author_parent_class)->finalize (object);
}

static void
gitg_author_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GitgAuthor *author = GITG_AUTHOR(object);

	switch (prop_id)
	{
		case PROP_NAME:
			g_value_set_string(value, author->priv->name);
			break;
		case PROP_EMAIL:
			g_value_set_string(value, author->priv->email);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void
gitg_author_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GitgAuthor *author = GITG_AUTHOR(object);
	
	switch (prop_id)
	{
		case PROP_NAME:
			g_free (author->priv->name);
			author->priv->name = g_strdup (g_value_get_string (value));
			break;
		case PROP_EMAIL:
			g_free (author->priv->email);
			author->priv->email = g_strdup (g_value_get_string (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gitg_author_class_init (GitgAuthorClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	GObjectClass* parent_class = G_OBJECT_CLASS (klass);

	object_class->set_property = gitg_author_set_property;
	object_class->get_property = gitg_author_get_property;
	object_class->finalize     = gitg_author_finalize;

	g_object_class_install_property (object_class, PROP_NAME,
					 g_param_spec_string ("name", "Name",
							      "The name of the author",
							      NULL, G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_EMAIL,
					 g_param_spec_string ("email", "Email",
							      "The email address of the author",
							      NULL, G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (GitgAuthorPrivate));
}

void
gitg_author_set_email(GitgAuthor *author, gchar const *email)
{
	g_return_if_fail (GITG_IS_AUTHOR (author));
	g_object_set (author, "email", email, NULL);
}

const gchar *
gitg_author_get_email(GitgAuthor* author)
{
	g_return_val_if_fail (GITG_IS_AUTHOR (author), NULL);
	return author->priv->email;
}

void
gitg_author_set_name (GitgAuthor *author, gchar const *name)
{
	g_return_if_fail (GITG_IS_AUTHOR (author));
	g_object_set (author, "name", name, NULL);
}

const gchar *
gitg_author_get_name(GitgAuthor* author)
{
	g_return_val_if_fail(GITG_IS_AUTHOR (author), NULL);
	return author->priv->name;
}
