/*
 * gitg-repository.c
 * This file is part of gitg - git repository viewer
 *
 * Copyright (C) 2009 - Jesse van den Kieboom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA.
 */

#include "gitg-repository.h"
#include "gitg-utils.h"
#include "gitg-lanes.h"
#include "gitg-ref.h"
#include "gitg-types.h"
#include "gitg-preferences.h"
#include "gitg-data-binding.h"
#include "gitg-config.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#define GITG_REPOSITORY_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), GITG_TYPE_REPOSITORY, GitgRepositoryPrivate))

static void gitg_repository_tree_model_iface_init(GtkTreeModelIface *iface);

G_DEFINE_TYPE_EXTENDED(GitgRepository, gitg_repository, G_TYPE_OBJECT, 0,
	G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL, gitg_repository_tree_model_iface_init));

/* Properties */
enum
{
	PROP_0,
	
	PROP_PATH,
	PROP_LOADER
};

/* Signals */
enum
{
	LOAD,
	LAST_SIGNAL
};

static guint repository_signals[LAST_SIGNAL] = { 0 };

enum
{
	OBJECT_COLUMN,
	SUBJECT_COLUMN,
	AUTHOR_COLUMN,
	DATE_COLUMN,
	N_COLUMNS
};

typedef enum
{
	LOAD_STAGE_NONE = 0,
	LOAD_STAGE_STASH,
	LOAD_STAGE_STAGED,
	LOAD_STAGE_UNSTAGED,
	LOAD_STAGE_COMMITS,
	LOAD_STAGE_LAST
} LoadStage;

struct _GitgRepositoryPrivate
{
	gchar *path;
	GitgRunner *loader;
	GHashTable *hashtable;
	gint stamp;
	GType column_types[N_COLUMNS];
	
	GitgRevision **storage;
	GitgLanes *lanes;
	GHashTable *refs;
	GitgRef *current_ref;
	GitgRef *working_ref;

	gulong size;
	gulong allocated;
	gint grow_size;
	
	gchar **last_args;
	guint idle_relane_id;
	
	LoadStage load_stage;
	
	GFileMonitor *monitor;
};

inline static gint
gitg_repository_error_quark()
{
	static GQuark quark = 0;
	
	if (G_UNLIKELY(quark == 0))
		quark = g_quark_from_static_string("GitgRepositoryErrorQuark");
	
	return quark;
}

/* GtkTreeModel implementations */
static GtkTreeModelFlags 
tree_model_get_flags(GtkTreeModel *tree_model)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), 0);
	return GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY;
}

static gint 
tree_model_get_n_columns(GtkTreeModel *tree_model)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), 0);
	return N_COLUMNS;
}

static GType 
tree_model_get_column_type(GtkTreeModel *tree_model, gint index)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), G_TYPE_INVALID);
	g_return_val_if_fail(index < N_COLUMNS && index >= 0, G_TYPE_INVALID);
	
	return GITG_REPOSITORY(tree_model)->priv->column_types[index];
}

static void
fill_iter(GitgRepository *repository, gint index, GtkTreeIter *iter)
{
	iter->stamp = repository->priv->stamp;
	iter->user_data = GINT_TO_POINTER(index);
}

static gboolean
tree_model_get_iter(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreePath *path)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), FALSE);
	
	gint *indices;
	gint depth;

	indices = gtk_tree_path_get_indices(path);
	depth = gtk_tree_path_get_depth(path);
	
	GitgRepository *rp = GITG_REPOSITORY(tree_model);

	g_return_val_if_fail(depth == 1, FALSE);
	
	if (indices[0] < 0 || indices[0] >= rp->priv->size)
		return FALSE;
	
	fill_iter(rp, indices[0], iter);
	
	return TRUE;
}

static GtkTreePath *
tree_model_get_path(GtkTreeModel *tree_model, GtkTreeIter *iter)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), NULL);
	
	GitgRepository *rp = GITG_REPOSITORY(tree_model);
	g_return_val_if_fail(iter->stamp == rp->priv->stamp, NULL);
	
	return gtk_tree_path_new_from_indices(GPOINTER_TO_INT(iter->user_data), -1);
}

static void 
tree_model_get_value(GtkTreeModel *tree_model, GtkTreeIter *iter, gint column, GValue *value)
{
	g_return_if_fail(GITG_IS_REPOSITORY(tree_model));
	g_return_if_fail(column >= 0 && column < N_COLUMNS);
	
	GitgRepository *rp = GITG_REPOSITORY(tree_model);
	g_return_if_fail(iter->stamp == rp->priv->stamp);

	gint index = GPOINTER_TO_INT(iter->user_data);
	
	g_return_if_fail(index >= 0 && index < rp->priv->size);
	GitgRevision *rv = rp->priv->storage[index];
	
	g_value_init(value, rp->priv->column_types[column]);

	switch (column)
	{
		case OBJECT_COLUMN:
			g_value_set_boxed(value, rv);
		break;
		case SUBJECT_COLUMN:
			g_value_set_string(value, gitg_revision_get_subject(rv));
		break;
		case AUTHOR_COLUMN:
			g_value_set_string(value, gitg_revision_get_author(rv));
		break;
		case DATE_COLUMN:
			g_value_take_string(value, gitg_utils_timestamp_to_str(gitg_revision_get_timestamp(rv)));
		break;
		default:
			g_assert_not_reached();
		break;
	}
}

static gboolean
tree_model_iter_next(GtkTreeModel *tree_model, GtkTreeIter *iter)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), FALSE);

	GitgRepository *rp = GITG_REPOSITORY(tree_model);
	g_return_val_if_fail(iter->stamp == rp->priv->stamp, FALSE);
	
	gint next = GPOINTER_TO_INT(iter->user_data) + 1;
	
	if (next >= rp->priv->size)
		return FALSE;
	
	iter->user_data = GINT_TO_POINTER(next);
	return TRUE;
}

static gboolean
tree_model_iter_children(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *parent)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), FALSE);

	// Only root has children, because it's a flat list
	if (parent != NULL)
		return FALSE;
	
	GitgRepository *rp = GITG_REPOSITORY(tree_model);
	fill_iter(rp, 0, iter);
	
	return TRUE;
}

static gboolean
tree_model_iter_has_child(GtkTreeModel *tree_model, GtkTreeIter *iter)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), FALSE);
	
	// Only root (NULL) has children
	return iter == NULL;
}

static gint
tree_model_iter_n_children(GtkTreeModel *tree_model, GtkTreeIter *iter)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), 0);
	GitgRepository *rp = GITG_REPOSITORY(tree_model);
	
	return iter ? 0 : rp->priv->size;
}

static gboolean
tree_model_iter_nth_child(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *parent, gint n)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), FALSE);
	g_return_val_if_fail(n >= 0, FALSE);

	if (parent)
		return FALSE;

	GitgRepository *rp = GITG_REPOSITORY(tree_model);	
	g_return_val_if_fail(n < rp->priv->size, FALSE);
	
	fill_iter(rp, n, iter);
	
	return TRUE;
}

static gboolean 
tree_model_iter_parent(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *child)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(tree_model), FALSE);
	return FALSE;
}

static void
gitg_repository_tree_model_iface_init(GtkTreeModelIface *iface)
{
	iface->get_flags = tree_model_get_flags;
	iface->get_n_columns = tree_model_get_n_columns;
	iface->get_column_type = tree_model_get_column_type;
	iface->get_iter = tree_model_get_iter;
	iface->get_path = tree_model_get_path;
	iface->get_value = tree_model_get_value;
	iface->iter_next = tree_model_iter_next;
	iface->iter_children = tree_model_iter_children;
	iface->iter_has_child = tree_model_iter_has_child;
	iface->iter_n_children = tree_model_iter_n_children;
	iface->iter_nth_child = tree_model_iter_nth_child;
	iface->iter_parent = tree_model_iter_parent;
}

static void
do_clear(GitgRepository *repository, gboolean emit)
{
	gint i;
	GtkTreePath *path = gtk_tree_path_new_from_indices(repository->priv->size - 1, -1);
	
	for (i = repository->priv->size - 1; i >= 0; --i)
	{
		if (emit)
		{
			GtkTreePath *dup = gtk_tree_path_copy(path);
			gtk_tree_model_row_deleted(GTK_TREE_MODEL(repository), dup);
			gtk_tree_path_free(dup);
		}
		
		gtk_tree_path_prev(path);
		gitg_revision_unref(repository->priv->storage[i]);
	}
	
	gtk_tree_path_free(path);
	
	if (repository->priv->storage)
		g_slice_free1(sizeof(GitgRevision *) * repository->priv->size, repository->priv->storage);
	
	repository->priv->storage = NULL;
	repository->priv->size = 0;
	repository->priv->allocated = 0;
	
	gitg_ref_free(repository->priv->current_ref);
	repository->priv->current_ref = NULL;
	
	/* clear hash tables */
	g_hash_table_remove_all(repository->priv->hashtable);
	g_hash_table_remove_all(repository->priv->refs);
	
	gitg_color_reset();
}

static void
gitg_repository_finalize(GObject *object)
{
	GitgRepository *rp = GITG_REPOSITORY(object);
	
	/* Make sure to cancel the loader */
	gitg_runner_cancel(rp->priv->loader);
	g_object_unref(rp->priv->loader);
	
	g_object_unref(rp->priv->lanes);
	
	/* Clear the model to remove all revision objects */
	do_clear(rp, FALSE);
	
	/* Free the path */
	g_free(rp->priv->path);
	
	/* Free the hash */
	g_hash_table_destroy(rp->priv->hashtable);
	g_hash_table_destroy(rp->priv->refs);
	
	/* Free cached args */
	g_strfreev(rp->priv->last_args);
	
	if (rp->priv->idle_relane_id)
	{
		g_source_remove(rp->priv->idle_relane_id);
	}
	
	if (rp->priv->current_ref)
	{
		gitg_ref_free (rp->priv->current_ref);
	}
	
	if (rp->priv->working_ref)
	{
		gitg_ref_free (rp->priv->working_ref);
	}

	if (rp->priv->monitor)
	{
		g_file_monitor_cancel (rp->priv->monitor);
		g_object_unref (rp->priv->monitor);
	}

	G_OBJECT_CLASS (gitg_repository_parent_class)->finalize(object);
}

static void
gitg_repository_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GitgRepository *self = GITG_REPOSITORY(object);
	
	switch (prop_id)
	{
		case PROP_PATH:
			g_free(self->priv->path);
			self->priv->path = gitg_utils_find_git(g_value_get_string(value));
		break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
gitg_repository_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GitgRepository *self = GITG_REPOSITORY(object);
	
	switch (prop_id)
	{
		case PROP_PATH:
			g_value_set_string(value, self->priv->path);
		break;
		case PROP_LOADER:
			g_value_set_object(value, self->priv->loader);
		break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static gchar *
parse_ref_intern (GitgRepository *repository, gchar const *ref, gboolean symbolic)
{
	gchar **ret = gitg_repository_command_with_outputv(repository, NULL, "rev-parse", "--verify", symbolic ? "--symbolic-full-name" : ref, symbolic ? ref : NULL, NULL);
	
	if (!ret)
		return NULL;
	
	gchar *r = g_strdup(*ret);
	g_strfreev(ret);
	
	return r;	
}

static GitgRef *
get_current_working_ref(GitgRepository *repository)
{
	GitgRef *ret = NULL;
	
	gchar *hash = parse_ref_intern (repository, "HEAD", FALSE);
	gchar *name = parse_ref_intern (repository, "HEAD", TRUE);
	
	if (hash && name)
	{
		ret = gitg_ref_new (hash, name);
	}
	
	g_free (hash);
	g_free (name);
	
	return ret;
}

static void
on_head_changed (GFileMonitor      *monitor,
                 GFile             *file,
                 GFile             *otherfile,
                 GFileMonitorEvent  event,
                 GitgRepository    *repository)
{
	switch (event)
	{
		case G_FILE_MONITOR_EVENT_CHANGED:
		case G_FILE_MONITOR_EVENT_CREATED:
		{
			GitgRef *current = get_current_working_ref (repository);
			
			if (!gitg_ref_equal (current, repository->priv->working_ref))
			{
				gitg_repository_reload (repository);
			}
			
			gitg_ref_free (current);
		}
		break;
		default:
		break;
	}
}

static void
install_head_monitor (GitgRepository *repository)
{
	gchar *path = g_build_filename (repository->priv->path, ".git", "HEAD", NULL);
	GFile *file = g_file_new_for_path (path);
	
	repository->priv->monitor = g_file_monitor_file (file, 
	                                                 G_FILE_MONITOR_NONE,
	                                                 NULL,
	                                                 NULL);

	g_signal_connect (repository->priv->monitor, 
	                  "changed", 
	                  G_CALLBACK (on_head_changed),
	                  repository);

	g_free (path);
	g_object_unref (file);
}

static GObject *
gitg_repository_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
	GObject *ret = G_OBJECT_CLASS (gitg_repository_parent_class)->constructor (type,
	                                                                           n_construct_properties,
	                                                                           construct_properties);

	install_head_monitor (GITG_REPOSITORY (ret));
	
	return ret;
}

static void 
gitg_repository_class_init(GitgRepositoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = gitg_repository_finalize;
	
	object_class->set_property = gitg_repository_set_property;
	object_class->get_property = gitg_repository_get_property;
	
	object_class->constructor = gitg_repository_constructor;
	
	g_object_class_install_property(object_class, PROP_PATH,
						 g_param_spec_string ("path",
								      "PATH",
								      "The repository path",
								      NULL,
								      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	
	g_object_class_install_property(object_class, PROP_LOADER,
						 g_param_spec_object ("loader",
								      "LOADER",
								      "The repository loader",
								      GITG_TYPE_RUNNER,
								      G_PARAM_READABLE));
	
	repository_signals[LOAD] =
   		g_signal_new ("load",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GitgRepositoryClass, load),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private(object_class, sizeof(GitgRepositoryPrivate));
}

static void
append_revision(GitgRepository *repository, GitgRevision *rv)
{
	GSList *lanes;
	gint8 mylane = 0;
	
	if (repository->priv->size == 0)
		gitg_lanes_reset(repository->priv->lanes);

	lanes = gitg_lanes_next(repository->priv->lanes, rv, &mylane);
	gitg_revision_set_lanes(rv, lanes, mylane);

	gitg_repository_add(repository, rv, NULL);
	gitg_revision_unref(rv);
}

static void
add_dummy_commit(GitgRepository *repository, gboolean staged)
{
	GitgRevision *revision;
	gchar const *subject;
	struct timeval tv;
	
	gettimeofday(&tv, NULL);
	
	if (staged)
		subject = _("Staged changes");
	else
		subject = _("Unstaged changes");

	revision = gitg_revision_new("0000000000000000000000000000000000000000", "", subject, NULL, tv.tv_sec);
	gitg_revision_set_sign(revision, staged ? 't' : 'u');

	append_revision(repository, revision);
}

static void
on_loader_end_loading(GitgRunner *object, gboolean cancelled, GitgRepository *repository)
{
	if (cancelled)
		return;

	LoadStage current = repository->priv->load_stage++;
	GitgPreferences *preferences = gitg_preferences_get_default();
	gboolean show_unstaged;
	gboolean show_staged;
	
	g_object_get(preferences, 
	             "history-show-virtual-staged", &show_staged, 
	             "history-show-virtual-unstaged", &show_unstaged,
	             NULL);

	switch (current)
	{
		case LOAD_STAGE_STASH:
		case LOAD_STAGE_STAGED:
		{
			/* Check if there are staged changes */
			gchar *head = gitg_repository_parse_head(repository);
			const gchar *cached = NULL;
			
			if (current == LOAD_STAGE_STAGED)
			{
				/* Check if there are unstaged changes */
				if (show_staged && gitg_runner_get_exit_status(object) != 0)
				{
					add_dummy_commit(repository, TRUE);
				}
			}
			else
			{
				cached = "--cached";
			}
			
			gitg_repository_run_commandv(repository, object, NULL, "diff-index", "--quiet", head, cached, NULL);
			g_free(head);
		}
		break;
		case LOAD_STAGE_UNSTAGED:
			if (show_unstaged && gitg_runner_get_exit_status(object) != 0)
			{
				add_dummy_commit(repository, FALSE);
			}

			gitg_repository_run_command(repository, object, (gchar const **)repository->priv->last_args, NULL);

		break;
		default:
		break;
	}
}

static gint
find_ref_custom (GitgRef *first, GitgRef *second)
{
	return gitg_ref_equal (first, second) ? 0 : 1;
}

static GitgRef *
add_ref(GitgRepository *self, gchar const *sha1, gchar const *name)
{
	GitgRef *ref = gitg_ref_new(sha1, name);
	GSList *refs = (GSList *)g_hash_table_lookup(self->priv->refs, 
	                                             gitg_ref_get_hash(ref));
	
	if (refs == NULL)
	{
		g_hash_table_insert(self->priv->refs, 
		                    (gpointer)gitg_ref_get_hash(ref), 
		                    g_slist_append(NULL, ref));
	}
	else
	{
		if (!g_slist_find_custom (refs, ref, (GCompareFunc)find_ref_custom))
		{
			refs = g_slist_append(refs, ref);
		}
		else
		{
			gitg_ref_free (ref);
		}
	}
	
	return ref;
}

static void
loader_update_stash(GitgRepository *repository, gchar **buffer)
{
	gchar *line;
	GitgPreferences *preferences = gitg_preferences_get_default();
	gboolean show_stash;
	
	g_object_get(preferences, "history-show-virtual-stash", &show_stash, NULL);

	if (!show_stash)
		return;
	
	while ((line = *buffer++) != NULL)
	{
		gchar **components = g_strsplit(line, "\01", 0);
		guint len = g_strv_length(components);
		
		if (len < 4)
		{
			g_strfreev(components);
			continue;
		}
		
		/* components -> [hash, author, subject, timestamp] */
		gint64 timestamp = g_ascii_strtoll(components[3], NULL, 0);
		GitgRevision *rv = gitg_revision_new(components[0], components[1], components[2], NULL, timestamp);
		
		add_ref (repository, components[0], "refs/stash");
		
		gitg_revision_set_sign(rv, 's');
		append_revision(repository, rv);
		g_strfreev(components);
	}
}

static void
loader_update_commits(GitgRepository *self, gchar **buffer)
{
	gchar *line;
	
	while ((line = *buffer++) != NULL)
	{
		/* new line is read */
		gchar **components = g_strsplit(line, "\01", 0);
		guint len = g_strv_length(components);
		
		if (len < 5)
		{
			g_strfreev(components);
			continue;
		}

		/* components -> [hash, author, subject, parents ([1 2 3]), timestamp[, leftright]] */
		gint64 timestamp = g_ascii_strtoll(components[4], NULL, 0);
	
		GitgRevision *rv = gitg_revision_new(components[0], components[1], components[2], components[3], timestamp);
		
		if (len > 5 && strlen(components[5]) == 1 && strchr("<>-^", *components[5]) != NULL)
		{
			gitg_revision_set_sign(rv, *components[5]);
		}

		append_revision(self, rv);
		g_strfreev(components);
	}
}

static void
on_loader_update(GitgRunner *object, gchar **buffer, GitgRepository *repository)
{
	switch (repository->priv->load_stage)
	{
		case LOAD_STAGE_STASH:
			loader_update_stash(repository, buffer);
		break;
		case LOAD_STAGE_STAGED:
		break;
		case LOAD_STAGE_UNSTAGED:
		break;
		case LOAD_STAGE_COMMITS:
			loader_update_commits(repository, buffer);
		break;
		default:
		break;
	}
}

static void
free_refs(GSList *refs)
{
	g_slist_foreach(refs, (GFunc)gitg_ref_free, NULL);
	g_slist_free(refs);
}

static gboolean
repository_relane(GitgRepository *repository)
{
	repository->priv->idle_relane_id = 0;
	
	gitg_lanes_reset(repository->priv->lanes);
	
	guint i;
	GtkTreeIter iter;
	GtkTreePath *path = gtk_tree_path_new_first();
	
	
	for (i = 0; i < repository->priv->size; ++i)
	{
		gint8 mylane;
		GitgRevision *revision = repository->priv->storage[i];

		GSList *lanes = gitg_lanes_next(repository->priv->lanes, revision, &mylane);
		gitg_revision_set_lanes(revision, lanes, mylane);

		fill_iter(repository, i, &iter);
		gtk_tree_model_row_changed(GTK_TREE_MODEL(repository), path, &iter);
		
		gtk_tree_path_next(path);
	}
	
	gtk_tree_path_free(path);
	
	return FALSE;
}

static void
prepare_relane(GitgRepository *repository)
{
	if (!repository->priv->idle_relane_id)
		repository->priv->idle_relane_id = g_idle_add((GSourceFunc)repository_relane, repository);
}

static gboolean
convert_setting_to_inactive_max(GValue const *setting, GValue *value, gpointer userdata)
{
	g_return_val_if_fail(G_VALUE_HOLDS(setting, G_TYPE_INT), FALSE);
	g_return_val_if_fail(G_VALUE_HOLDS(value, G_TYPE_INT), FALSE);
	
	gint s = g_value_get_int(setting);
	g_value_set_int(value, 2 + s * 8);
	
	prepare_relane(GITG_REPOSITORY(userdata));
	return TRUE;
}

static gboolean
convert_setting_to_inactive_collapse(GValue const *setting, GValue *value, gpointer userdata)
{
	g_return_val_if_fail(G_VALUE_HOLDS(setting, G_TYPE_INT), FALSE);
	g_return_val_if_fail(G_VALUE_HOLDS(value, G_TYPE_INT), FALSE);

	gint s = g_value_get_int(setting);
	g_value_set_int(value, 1 + s * 3);

	prepare_relane(GITG_REPOSITORY(userdata));	
	return TRUE;
}

static gboolean
convert_setting_to_inactive_gap(GValue const *setting, GValue *value, gpointer userdata)
{
	g_return_val_if_fail(G_VALUE_HOLDS(setting, G_TYPE_INT), FALSE);
	g_return_val_if_fail(G_VALUE_HOLDS(value, G_TYPE_INT), FALSE);

	g_value_set_int(value, 10);
	
	prepare_relane(GITG_REPOSITORY(userdata));	
	return TRUE;
}

static gboolean
convert_setting_to_inactive_enabled(GValue const *setting, GValue *value, gpointer userdata)
{
	g_return_val_if_fail(G_VALUE_HOLDS(setting, G_TYPE_BOOLEAN), FALSE);
	g_return_val_if_fail(G_VALUE_HOLDS(value, G_TYPE_BOOLEAN), FALSE);

	gboolean s = g_value_get_boolean(setting);
	g_value_set_boolean(value, s);
	
	prepare_relane(GITG_REPOSITORY(userdata));	
	return TRUE;
}

static void
on_update_virtual(GObject *object, GParamSpec *spec, GitgRepository *repository)
{
	gitg_repository_reload (repository);
}

static void
initialize_bindings(GitgRepository *repository)
{
	GitgPreferences *preferences = gitg_preferences_get_default();
	
	gitg_data_binding_new_full(preferences, "history-collapse-inactive-lanes",
							   repository->priv->lanes, "inactive-max",
							   convert_setting_to_inactive_max,
							   repository);

	gitg_data_binding_new_full(preferences, "history-collapse-inactive-lanes",
							   repository->priv->lanes, "inactive-collapse",
							   convert_setting_to_inactive_collapse,
							   repository);

	gitg_data_binding_new_full(preferences, "history-collapse-inactive-lanes",
							   repository->priv->lanes, "inactive-gap",
							   convert_setting_to_inactive_gap,
							   repository);		

	gitg_data_binding_new_full(preferences, "history-collapse-inactive-lanes-active",
	                           repository->priv->lanes, "inactive-enabled",
	                           convert_setting_to_inactive_enabled,
	                           repository);	

	g_signal_connect(preferences, 
	                 "notify::history-show-virtual-stash",
	                 G_CALLBACK(on_update_virtual),
	                 repository);

	g_signal_connect(preferences, 
	                 "notify::history-show-virtual-unstaged",
	                 G_CALLBACK(on_update_virtual),
	                 repository);

	g_signal_connect(preferences, 
	                 "notify::history-show-virtual-staged",
	                 G_CALLBACK(on_update_virtual),
	                 repository);
}

static void
gitg_repository_init(GitgRepository *object)
{
	object->priv = GITG_REPOSITORY_GET_PRIVATE(object);
	object->priv->hashtable = g_hash_table_new_full(gitg_utils_hash_hash, gitg_utils_hash_equal, NULL, NULL);
	
	object->priv->column_types[0] = GITG_TYPE_REVISION;
	object->priv->column_types[1] = G_TYPE_STRING;
	object->priv->column_types[2] = G_TYPE_STRING;
	object->priv->column_types[3] = G_TYPE_STRING;
	
	object->priv->lanes = gitg_lanes_new();
	object->priv->grow_size = 1000;
	object->priv->stamp = g_random_int();
	object->priv->refs = g_hash_table_new_full(gitg_utils_hash_hash, gitg_utils_hash_equal, NULL, (GDestroyNotify)free_refs);
	
	object->priv->loader = gitg_runner_new(10000);
	g_signal_connect(object->priv->loader, "update", G_CALLBACK(on_loader_update), object);
	g_signal_connect(object->priv->loader, "end-loading", G_CALLBACK(on_loader_end_loading), object);
	
	initialize_bindings(object);
}

static void
grow_storage(GitgRepository *repository, gint size)
{
	if (repository->priv->size + size <= repository->priv->allocated)
		return;
	
	gulong prevallocated = repository->priv->allocated;
	repository->priv->allocated += repository->priv->grow_size;
	GitgRevision **newstorage = g_slice_alloc(sizeof(GitgRevision *) * repository->priv->allocated);
	
	int i;
	for (i = 0; i < repository->priv->size; ++i)
		newstorage[i] = repository->priv->storage[i];
	
	if (repository->priv->storage)
		g_slice_free1(sizeof(GitgRevision *) * prevallocated, repository->priv->storage);
		
	repository->priv->storage = newstorage;
}

GitgRepository *
gitg_repository_new(gchar const *path)
{
	return g_object_new(GITG_TYPE_REPOSITORY, "path", path, NULL);
}

gchar const *
gitg_repository_get_path(GitgRepository *self)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(self), NULL);
	
	return self->priv->path;
}

GitgRunner *
gitg_repository_get_loader(GitgRepository *self)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(self), NULL);
	return GITG_RUNNER(g_object_ref(self->priv->loader));
}

static gboolean
has_left_right(gchar const **av, int argc)
{
	int i;

	for (i = 0; i < argc; ++i)
		if (strcmp(av[i], "--left-right") == 0)
			return TRUE;
	
	return FALSE;
}

static gboolean
reload_revisions(GitgRepository *repository, GError **error)
{
	if (repository->priv->working_ref)
	{
		gitg_ref_free (repository->priv->working_ref);
		repository->priv->working_ref = NULL;
	}

	g_signal_emit(repository, repository_signals[LOAD], 0);
	
	repository->priv->load_stage = LOAD_STAGE_STASH;
	
	return gitg_repository_run_commandv(repository, repository->priv->loader, error, "log", "--pretty=format:%H\x01%an\x01%s\x01%at", "--encoding=UTF-8", "-g", "refs/stash", NULL);
}

static void
build_log_args(GitgRepository *self, gint argc, gchar const **av)
{
	gchar **argv = g_new0(gchar *, 6 + (argc > 0 ? argc - 1 : 0));

	argv[0] = g_strdup("log");
	
	if (has_left_right(av, argc))
	{
		argv[1] = g_strdup("--pretty=format:%H\x01%an\x01%s\x01%P\x01%at\x01%m");
	}
	else
	{
		argv[1] = g_strdup("--pretty=format:%H\x01%an\x01%s\x01%P\x01%at");
	}
	
	argv[2] = g_strdup ("--encoding=UTF-8");
	
	gchar *head = NULL;
	
	if (argc <= 0)
	{
		head = gitg_repository_parse_ref(self, "HEAD");
		
		if (head)
		{
			argv[3] = g_strdup("HEAD");
		}
		
		g_free(head);
	}
	else
	{
		int i;

		for (i = 0; i < argc; ++i)
		{
			argv[3 + i] = g_strdup(av[i]);
		}
	}

	g_strfreev(self->priv->last_args);
	self->priv->last_args = argv;
}

static gchar *
load_current_ref(GitgRepository *self)
{
	gchar **out;
	gchar *ret = NULL;
	gint i;
	gint numargs;

	numargs = g_strv_length(self->priv->last_args);

	gchar const **argv = g_new0(gchar const *, numargs + 3);

	argv[0] = "rev-parse";
	argv[1] = "--no-flags";
	argv[2] = "--symbolic-full-name";

	for (i = 1; i < numargs; ++i)
	{
		argv[2 + i] = self->priv->last_args[i];
	}

	out = gitg_repository_command_with_output(self, argv, NULL);
	
	if (!out)
	{
		return NULL;
	}
	
	if (*out && !*(out + 1))
	{
		ret = g_strdup(*out);
	}
	
	g_strfreev(out);
	return ret;
}

static void
load_refs(GitgRepository *self)
{
	gchar **refs = gitg_repository_command_with_outputv(self, NULL, "for-each-ref", "--format=%(refname) %(objectname) %(*objectname)", "refs", NULL);
	
	if (!refs)
	{
		return;
	}
		
	gchar **buffer = refs;
	gchar *buf;
	gchar *current = load_current_ref(self);
	
	while ((buf = *buffer++) != NULL)
	{
		// each line will look like <name> <hash>
		gchar **components = g_strsplit(buf, " ", 3);
		guint len = g_strv_length(components);
		
		if (len == 2 || len == 3)
		{
			gchar const *obj = len == 3 && *components[2] ? components[2] : components[1];
			GitgRef *ref = add_ref(self, obj, components[0]);
		
			if (current != NULL && strcmp(gitg_ref_get_name(ref), current) == 0)
			{
				self->priv->current_ref = gitg_ref_copy(ref);
			}
		}
		
		g_strfreev(components);
	}

	g_strfreev(refs);
	g_free(current);
}

void
gitg_repository_reload(GitgRepository *repository)
{
	g_return_if_fail(GITG_IS_REPOSITORY(repository));
	g_return_if_fail(repository->priv->path != NULL);

	gitg_runner_cancel(repository->priv->loader);
	gitg_repository_clear(repository);
	
	load_refs(repository);
	reload_revisions(repository, NULL);
}

gboolean
gitg_repository_load(GitgRepository *self, int argc, gchar const **av, GError **error)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(self), FALSE);
	
	if (self->priv->path == NULL)
	{
		if (error)
		{
			*error = g_error_new_literal(gitg_repository_error_quark(), GITG_REPOSITORY_ERROR_NOT_FOUND, _("Not a valid git repository"));
		}
			
		return FALSE;
	}

	gitg_runner_cancel(self->priv->loader);
	gitg_repository_clear(self);
		
	build_log_args(self, argc, av);
	
	/* first get the refs */
	load_refs(self);
	
	/* request log (all the revision) */
	return reload_revisions(self, error);
}

void
gitg_repository_add(GitgRepository *self, GitgRevision *obj, GtkTreeIter *iter)
{
	GtkTreeIter iter1;

	/* validate our parameters */
	g_return_if_fail(GITG_IS_REPOSITORY(self));
	
	grow_storage(self, 1);

	/* put this object in our data storage */
	self->priv->storage[self->priv->size++] = gitg_revision_ref(obj);

	g_hash_table_insert(self->priv->hashtable, (gpointer)gitg_revision_get_hash(obj), GUINT_TO_POINTER(self->priv->size - 1));

	iter1.stamp = self->priv->stamp;
	iter1.user_data = GINT_TO_POINTER(self->priv->size - 1);
	iter1.user_data2 = NULL;
	iter1.user_data3 = NULL;
	
	GtkTreePath *path = gtk_tree_path_new_from_indices(self->priv->size - 1, -1);
	gtk_tree_model_row_inserted(GTK_TREE_MODEL(self), path, &iter1);
	gtk_tree_path_free(path);
	
	/* return the iter if the user cares */
	if (iter)
		*iter = iter1;
}

void
gitg_repository_clear(GitgRepository *repository)
{
	g_return_if_fail(GITG_IS_REPOSITORY(repository));
	do_clear(repository, TRUE);
}

GitgRevision *
gitg_repository_lookup(GitgRepository *store, gchar const *hash)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(store), NULL);
	
	gpointer result = g_hash_table_lookup(store->priv->hashtable, hash);
	
	if (!result)
		return NULL;
	
	return store->priv->storage[GPOINTER_TO_UINT(result)];
}

gboolean
gitg_repository_find_by_hash(GitgRepository *store, gchar const *hash, GtkTreeIter *iter)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(store), FALSE);
	
	gpointer result = g_hash_table_lookup(store->priv->hashtable, hash);
	
	if (!result)
		return FALSE;
	
	GtkTreePath *path = gtk_tree_path_new_from_indices(GPOINTER_TO_UINT(result), -1);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(store), iter, path);
	gtk_tree_path_free(path);

	return TRUE;
}

gboolean
gitg_repository_find(GitgRepository *store, GitgRevision *revision, GtkTreeIter *iter)
{
	return gitg_repository_find_by_hash(store, gitg_revision_get_hash(revision), iter);
}

GSList *
gitg_repository_get_refs(GitgRepository *repository)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	GList *values = g_hash_table_get_values(repository->priv->refs);
	GSList *ret = NULL;
	GList *item;
	
	for (item = values; item; item = item->next)
	{
		GSList *val;
		
		for (val = (GSList *)item->data; val; val = val->next)
		{
			ret = g_slist_prepend(ret, gitg_ref_copy((GitgRef *)val->data));
		}
	}
	
	ret = g_slist_reverse (ret);
	g_list_free(values);
	return ret;
}

GSList *
gitg_repository_get_refs_for_hash(GitgRepository *repository, gchar const *hash)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	return g_slist_copy((GSList *)g_hash_table_lookup(repository->priv->refs, hash));
}

GitgRef *
gitg_repository_get_current_ref(GitgRepository *repository)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	
	return repository->priv->current_ref;
}

gchar *
gitg_repository_relative(GitgRepository *repository, GFile *file)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	g_return_val_if_fail(repository->priv->path != NULL, NULL);
	
	GFile *parent = g_file_new_for_path(repository->priv->path);
	gchar *ret = g_file_get_relative_path(parent, file);
	g_object_unref(parent);
	
	return ret;
}

gboolean
gitg_repository_run_command_with_input(GitgRepository *repository, GitgRunner *runner, gchar const **argv, gchar const *input, GError **error)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), FALSE);
	g_return_val_if_fail(GITG_IS_RUNNER(runner), FALSE);
	g_return_val_if_fail(repository->priv->path != NULL, FALSE);
	
	guint num = g_strv_length((gchar **)argv);
	guint i;
	gchar const **args = g_new0(gchar const *, num + 2);
	args[0] = "git";	
	
	for (i = 0; i < num; ++i)
		args[i + 1] = argv[i];
	
	gboolean ret = gitg_runner_run_with_arguments(runner, args, repository->priv->path, input, error);
	g_free(args);
	
	return ret;
}

gboolean
gitg_repository_run_command(GitgRepository *repository, GitgRunner *runner, gchar const **argv, GError **error)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), FALSE);
	g_return_val_if_fail(GITG_IS_RUNNER(runner), FALSE);
	g_return_val_if_fail(repository->priv->path != NULL, FALSE);

	return gitg_repository_run_command_with_input(repository, runner, argv, NULL, error);
}

gboolean 
gitg_repository_command_with_input(GitgRepository *repository, gchar const **argv, gchar const *input, GError **error)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), FALSE);
	g_return_val_if_fail(repository->priv->path != NULL, FALSE);

	GitgRunner *runner = gitg_runner_new_synchronized(1000);
	
	gboolean ret = gitg_repository_run_command_with_input(repository, runner, argv, input, error);
	g_object_unref(runner);

	return ret;
}

gboolean
gitg_repository_command(GitgRepository *repository, gchar const **argv, GError **error)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), FALSE);
	g_return_val_if_fail(repository->priv->path != NULL, FALSE);

	return gitg_repository_command_with_input(repository, argv, NULL, error);
}

typedef struct
{
	gchar **buffer;
	guint size;
} CommandOutput;

static void
command_with_output_update(GitgRunner *runner, gchar **buffer, CommandOutput *output)
{
	guint num = g_strv_length(buffer);
	guint i;
	
	output->buffer = g_realloc(output->buffer, sizeof(gchar *) * (output->size + num + 1));
	
	for (i = 0; i < num; ++i)
		output->buffer[output->size + i] = g_strdup(buffer[i]);
	
	output->size += num;
	output->buffer[output->size] = NULL;
}

gchar **
gitg_repository_command_with_input_and_output(GitgRepository *repository, gchar const **argv, gchar const *input, GError **error)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	g_return_val_if_fail(repository->priv->path != NULL, NULL);
	
	GitgRunner *runner = gitg_runner_new_synchronized(1000);
	CommandOutput output = {NULL, 0};

	g_signal_connect(runner, "update", G_CALLBACK(command_with_output_update), &output);
	gboolean ret = gitg_repository_run_command_with_input(repository, runner, argv, input, error);
	
	if (!ret)
	{
		g_strfreev(output.buffer);
		output.buffer = NULL;
	}
	
	g_object_unref(runner);
	return output.buffer;
}

gchar **
gitg_repository_command_with_output(GitgRepository *repository, gchar const **argv, GError **error)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	g_return_val_if_fail(repository->priv->path != NULL, NULL);

	return gitg_repository_command_with_input_and_output(repository, argv, NULL, error);
}

static gchar const **
parse_valist(va_list ap)
{
	gchar const *a;
	gchar const **ret = NULL;
	guint num = 0;
	
	while ((a = va_arg(ap, gchar const *)) != NULL)
	{
		ret = g_realloc(ret, sizeof(gchar const *) * (++num + 1));
		ret[num - 1] = a;
	}
	
	ret[num] = NULL;
	return ret;
}

gboolean 
gitg_repository_commandv(GitgRepository *repository, GError **error, ...)
{
	va_list ap;
	va_start(ap, error);
	gchar const **argv = parse_valist(ap);
	va_end(ap);
	
	gboolean ret = gitg_repository_command(repository, argv, error);
	g_free(argv);
	return ret;
}

gboolean 
gitg_repository_command_with_inputv(GitgRepository *repository, gchar const *input, GError **error, ...)
{
	va_list ap;
	va_start(ap, error);
	gchar const **argv = parse_valist(ap);
	va_end(ap);
	
	gboolean ret = gitg_repository_command_with_input(repository, argv, input, error);
	g_free(argv);
	return ret;
}

gboolean 
gitg_repository_run_commandv(GitgRepository *repository, GitgRunner *runner, GError **error, ...)
{
	va_list ap;
	va_start(ap, error);
	gchar const **argv = parse_valist(ap);
	va_end(ap);
	
	gboolean ret = gitg_repository_run_command(repository, runner, argv, error);
	g_free(argv);
	return ret;
}

gboolean 
gitg_repository_run_command_with_inputv(GitgRepository *repository, GitgRunner *runner, gchar const *input, GError **error, ...)
{
	va_list ap;
	va_start(ap, error);
	gchar const **argv = parse_valist(ap);
	va_end(ap);
	
	gboolean ret = gitg_repository_run_command_with_input(repository, runner, argv, input, error);
	g_free(argv);
	return ret;
}

gchar **
gitg_repository_command_with_outputv(GitgRepository *repository, GError **error, ...)
{
	va_list ap;
	va_start(ap, error);
	gchar const **argv = parse_valist(ap);
	va_end(ap);
	
	gchar **ret = gitg_repository_command_with_output(repository, argv, error);
	g_free(argv);
	return ret;
}

gchar **
gitg_repository_command_with_input_and_outputv(GitgRepository *repository, gchar const *input, GError **error, ...)
{
	va_list ap;
	va_start(ap, error);
	gchar const **argv = parse_valist(ap);
	va_end(ap);
	
	gchar **ret = gitg_repository_command_with_input_and_output(repository, argv, input, error);
	g_free(argv);
	return ret;
}

gchar *
gitg_repository_parse_ref(GitgRepository *repository, gchar const *ref)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	
	return parse_ref_intern (repository, ref, FALSE);
}

gchar *
gitg_repository_parse_head(GitgRepository *repository)
{
	g_return_val_if_fail(GITG_IS_REPOSITORY(repository), NULL);
	
	gchar *ret = gitg_repository_parse_ref(repository, "HEAD");
	
	if (!ret)
		ret = g_strdup("4b825dc642cb6eb9a060e54bf8d69288fbee4904");
	
	return ret;
}

GitgRef *
gitg_repository_get_current_working_ref(GitgRepository *repository)
{
	if (repository->priv->working_ref)
	{
		return repository->priv->working_ref;
	}
	
	repository->priv->working_ref = get_current_working_ref (repository);	
	return repository->priv->working_ref;
}

gchar **
gitg_repository_get_remotes (GitgRepository *repository)
{
	g_return_val_if_fail (GITG_IS_REPOSITORY (repository), NULL);

	GitgConfig *config = gitg_config_new (repository);
	gchar *ret = gitg_config_get_value_regex (config, "remote\\..*\\.url");

	gchar **remotes = g_malloc (sizeof (gchar *));
	remotes[0] = NULL;
	
	if (!ret)
	{
		g_object_unref (config);
		return remotes;
	}
	
	gchar **lines = g_strsplit(ret, "\n", -1);
	gchar **ptr = lines;
	
	GRegex *regex = g_regex_new ("remote\\.(.+?)\\.url\\s+(.*)", 0, 0, NULL);
	gint num = 0;
	
	while (*ptr)
	{
		GMatchInfo *info = NULL;
		
		if (g_regex_match (regex, *ptr, 0, &info))
		{
			gchar *name = g_match_info_fetch (info, 1);
			
			remotes = g_realloc (ret, sizeof(gchar *) * (++num + 1));
			remotes[num - 1] = name;
		}
		
		g_match_info_free (info);
		++ptr;
	}
	
	remotes[num] = NULL;
	g_object_unref (config);
	return remotes;
}
