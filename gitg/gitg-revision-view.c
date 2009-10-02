/*
 * gitg-revision-view.c
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

#include <gtk/gtk.h>
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#include <gtksourceview/gtksourcestyleschememanager.h>
#include <string.h>

#include "gitg-revision-view.h"
#include "gitg-diff-view.h"
#include "gitg-revision.h"
#include "gitg-runner.h"
#include "gitg-utils.h"

#define GITG_REVISION_VIEW_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE((object), GITG_TYPE_REVISION_VIEW, GitgRevisionViewPrivate))

/* Properties */
enum
{
	PROP_0,
	PROP_REPOSITORY
};

/* Signals */
enum
{
	PARENT_ACTIVATED,
	NUM_SIGNALS
};

static guint signals[NUM_SIGNALS];

typedef struct
{
	GitgDiffIter iter;
} CachedHeader;

struct _GitgRevisionViewPrivate
{
	GtkLabel *sha;
	GtkLinkButton *author;
	GtkLinkButton *committer;
	GtkLabel *date;
	GtkTable *parents;
	GtkTextView *log;
  
	GitgRunner *log_runner;
  
	GitgRepository *repository;
	GitgRevision *revision;
	GSList *cached_headers;
};

static void gitg_revision_view_buildable_iface_init(GtkBuildableIface *iface);
static void on_header_added(GitgDiffView *view, GitgDiffIter *iter, GitgRevisionView *self);
static void on_diff_files_selection_changed(GtkTreeSelection *selection, GitgRevisionView *self);

G_DEFINE_TYPE_EXTENDED(GitgRevisionView, gitg_revision_view, GTK_TYPE_VBOX, 0,
	G_IMPLEMENT_INTERFACE(GTK_TYPE_BUILDABLE, gitg_revision_view_buildable_iface_init));

static GtkBuildableIface parent_iface;

typedef enum
{
	DIFF_FILE_STATUS_NONE,
	DIFF_FILE_STATUS_NEW,
	DIFF_FILE_STATUS_MODIFIED,
	DIFF_FILE_STATUS_DELETED
} DiffFileStatus;

typedef struct
{
	gint refcount;

	gchar index_from[HASH_SHA_SIZE + 1];
	gchar index_to[HASH_SHA_SIZE + 1];
	DiffFileStatus status;
	gchar *filename;

	gboolean visible;
	GitgDiffIter iter;
} DiffFile;

static DiffFile *
diff_file_new(gchar const *from, gchar *to, gchar const *status, gchar const *filename)
{
	DiffFile *f = g_slice_new(DiffFile);
	
	strncpy(f->index_from, from, HASH_SHA_SIZE);
	strncpy(f->index_to, to, HASH_SHA_SIZE);
	
	f->index_from[HASH_SHA_SIZE] = '\0';
	f->index_to[HASH_SHA_SIZE] = '\0';
	f->visible = FALSE;
	
	DiffFileStatus st;
	
	switch (*status)
	{
		case 'A':
			st = DIFF_FILE_STATUS_NEW;
		break;
		case 'D':
			st = DIFF_FILE_STATUS_DELETED;
		break;
		default:
			st = DIFF_FILE_STATUS_MODIFIED;
		break;
	}
	
	f->status = st;
	f->filename = g_strdup(filename);
	f->refcount = 1;

	return f;
}

static DiffFile *
diff_file_copy(DiffFile *f)
{
	g_atomic_int_inc(&f->refcount);
	return f;
}

static void
diff_file_unref(DiffFile *f)
{
	if (!g_atomic_int_dec_and_test(&f->refcount))
		return;

	g_free(f->filename);
	g_slice_free(DiffFile, f);
}

static GType
diff_file_get_type()
{
	static GType gtype = 0;
	
	if (!G_UNLIKELY(gtype))
		gtype = g_boxed_type_register_static("DiffFile", (GBoxedCopyFunc)diff_file_copy, (GBoxedFreeFunc)diff_file_unref);
	
	return gtype;
}

static void
update_markup(GObject *object)
{
	GtkLabel *label = GTK_LABEL(object);
	gchar const *text = gtk_label_get_text(label);
	
	gchar *newtext = g_strconcat("<span weight='bold' foreground='#777'>", text, "</span>", NULL);

	gtk_label_set_markup(label, newtext);
	g_free(newtext);
}

static void
revision_files_icon(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, GitgRevisionView *self)
{
	DiffFile *f;
	gtk_tree_model_get(model, iter, 0, &f, -1);
	
	gchar const *id = NULL;
	
	switch (f->status)
	{
		case DIFF_FILE_STATUS_NEW:
			id = GTK_STOCK_NEW;
		break;
		case DIFF_FILE_STATUS_MODIFIED:
			id = GTK_STOCK_EDIT;
		break;
		case DIFF_FILE_STATUS_DELETED:
			id = GTK_STOCK_DELETE;
		break;
		default:
		break;
	}
	
	g_object_set(G_OBJECT(renderer), "stock-id", id, NULL);
	diff_file_unref(f);
}

static void
revision_files_name(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, GitgRevisionView *self)
{
	DiffFile *f;
	gtk_tree_model_get(model, iter, 0, &f, -1);
	
	g_object_set(G_OBJECT(renderer), "text", f->filename, NULL);
	
	diff_file_unref(f);
}

static gboolean
diff_file_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	DiffFile *f;
	gtk_tree_model_get(model, iter, 0, &f, -1);
	
	if (!f)
		return FALSE;

	gboolean ret = f->visible;
	diff_file_unref(f);
	
	return ret;
}

static gboolean
on_diff_files_button_press(GtkTreeView *treeview, GdkEventButton *event, GitgRevisionView *view)
{
	if (event->button != 1)
		return FALSE;
	
	if (event->window != gtk_tree_view_get_bin_window(treeview))
		return FALSE;

	GtkTreePath *path;
	if (!gtk_tree_view_get_path_at_pos(treeview, event->x, event->y, &path, NULL, NULL, NULL))
		return FALSE;
	
	GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
	gboolean ret = FALSE;
	
	if (gtk_tree_selection_path_is_selected(selection, path) && gtk_tree_selection_count_selected_rows(selection) == 1)
	{
		/* deselect */
		gtk_tree_selection_unselect_path(selection, path);
		ret = TRUE;
	}
	
	gtk_tree_path_free(path);
	return ret;
}

static void
gitg_revision_view_parser_finished(GtkBuildable *buildable, GtkBuilder *builder)
{
	if (parent_iface.parser_finished)
		parent_iface.parser_finished(buildable, builder);

	GitgRevisionView *rvv = GITG_REVISION_VIEW(buildable);

	rvv->priv->sha = GTK_LABEL(gtk_builder_get_object(builder, "label_sha"));
	rvv->priv->author = GTK_LINK_BUTTON(gtk_builder_get_object(builder, "label_author"));
	rvv->priv->committer = GTK_LINK_BUTTON(gtk_builder_get_object(builder, "label_committer"));
	rvv->priv->date = GTK_LABEL(gtk_builder_get_object(builder, "label_date"));
	rvv->priv->parents = GTK_TABLE(gtk_builder_get_object(builder, "table_parents"));
	rvv->priv->log = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_log"));
	
	gchar const *lbls[] = {
		"label_author_lbl",
		"label_committer_lbl",
		"label_sha_lbl",
		"label_date_lbl",
		"label_parent_lbl"
	};
	
	int i;
	for (i = 0; i < sizeof(lbls) / sizeof(gchar *); ++i)
		update_markup(gtk_builder_get_object(builder, lbls[i]));
}

static void
gitg_revision_view_buildable_iface_init(GtkBuildableIface *iface)
{
	parent_iface = *iface;
	
	iface->parser_finished = gitg_revision_view_parser_finished;
}

static void
free_cached_header(gpointer header)
{
	g_slice_free(CachedHeader, header);
}

static void
free_cached_headers(GitgRevisionView *self)
{
	g_slist_foreach(self->priv->cached_headers, (GFunc)free_cached_header, NULL);
	g_slist_free(self->priv->cached_headers);
	self->priv->cached_headers = NULL;
}

static void
gitg_revision_view_finalize(GObject *object)
{
	GitgRevisionView *self = GITG_REVISION_VIEW(object);
	
	gitg_runner_cancel(self->priv->log_runner);
	g_object_unref(self->priv->log_runner);

	if (self->priv->repository)
	{	
		g_object_unref(self->priv->repository);
	}
	
	free_cached_headers(self);

	G_OBJECT_CLASS(gitg_revision_view_parent_class)->finalize(object);
}

static void
gitg_revision_view_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GitgRevisionView *self = GITG_REVISION_VIEW(object);

	switch (prop_id)
	{
		case PROP_REPOSITORY:
			g_value_set_object(value, self->priv->repository);
		break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
gitg_revision_view_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GitgRevisionView *self = GITG_REVISION_VIEW(object);
	
	switch (prop_id)
	{
		case PROP_REPOSITORY:
		{
			if (self->priv->repository)
				g_object_unref(self->priv->repository);
				
			self->priv->repository = g_value_dup_object(value);
		}
		break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}


static void
gitg_revision_view_class_init(GitgRevisionViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	
	object_class->finalize = gitg_revision_view_finalize;

	object_class->set_property = gitg_revision_view_set_property;
	object_class->get_property = gitg_revision_view_get_property;

	g_object_class_install_property(object_class, PROP_REPOSITORY,
					 g_param_spec_object("repository",
							      "REPOSITORY",
							      "Repository",
							      GITG_TYPE_REPOSITORY,
							      G_PARAM_READWRITE));

	signals[PARENT_ACTIVATED] =
		g_signal_new("parent-activated",
			G_OBJECT_CLASS_TYPE (object_class),
			G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET (GitgRevisionViewClass, parent_activated),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE,
			1, G_TYPE_POINTER);
	
	g_type_class_add_private(object_class, sizeof(GitgRevisionViewPrivate));
}

static gboolean
match_indices(DiffFile *f, gchar const *from, gchar const *to)
{
	return g_str_has_prefix(f->index_from, from) && 
	       (g_str_has_prefix(f->index_to, to) ||
	        g_str_has_prefix(f->index_to, "0000000"));
}

static void
visible_from_cached_headers(GitgRevisionView *view, DiffFile *f)
{
	GSList *item;
	
	for (item = view->priv->cached_headers; item; item = g_slist_next(item))
	{
		CachedHeader *header = (CachedHeader *)item->data;
		gchar *from;
		gchar *to;
		
		gitg_diff_iter_get_index(&header->iter, &from, &to);

		if (gitg_diff_iter_get_index(&header->iter, &from, &to) && match_indices(f, from, to))
		{
			f->visible = TRUE;
			f->iter = header->iter;
			
			return;
		}
	}
}

static void
on_log_begin_loading(GitgRunner *runner, GitgRevisionView *self)
{
	GdkWindow *window = GTK_WIDGET(self->priv->log)->window;
	if (window != NULL)
	{
		GdkCursor *cursor = gdk_cursor_new(GDK_WATCH);
		gdk_window_set_cursor(window, cursor);
		gdk_cursor_unref(cursor);
	}
}

static void
on_log_end_loading(GitgRunner *runner, gboolean cancelled, GitgRevisionView *self)
{
	GdkWindow *window = GTK_WIDGET(self->priv->log)->window;
	if (window != NULL)
	{
		gdk_window_set_cursor(window, NULL);
	}
}

static void
on_log_update(GitgRunner *runner, gchar **buffer, GitgRevisionView *self)
{
	static GRegex *regex = NULL;
	GMatchInfo    *match = NULL;
	gchar *line;
	GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->log));
	GtkTextIter iter;
	gboolean first_line = TRUE;
	static GtkTextTag *tag_title = NULL;
	static GtkTextTag *tag_signed = NULL;
	
	if (!tag_title)
		tag_title = gtk_text_buffer_create_tag(buf, "title",
			"weight", PANGO_WEIGHT_BOLD,
		    NULL);
	if (!tag_signed)
		tag_signed = gtk_text_buffer_create_tag(buf, "signed",
			"style", PANGO_STYLE_ITALIC,
			NULL);
	
	if (G_UNLIKELY(!regex)) {
		regex = g_regex_new("^\\s*([^<]+?)?\\s*(?:<([^>]+)>)?\\s*$",
		                    G_REGEX_OPTIMIZE, 0, NULL);
	}

	gtk_text_buffer_get_end_iter(buf, &iter);
	
	while ((line = *buffer++))
	{
		if (line[0] == '\0')
		{
		  /* Blank line */
		  if (gtk_text_iter_is_start(&iter))
			/* Ignore the first blank line */
			line = NULL;
		}
		else if (line[0] == ' ')
		{
		  line = g_strchug (line);
		}
		else
		{
#define AUTHOR_KEY "Author: "
#define COMMITTER_KEY "Commit: "
			if (g_str_has_prefix(line, AUTHOR_KEY))
			{
				if (g_regex_match(regex, line+strlen(AUTHOR_KEY), 0, &match))
				{
					gtk_button_set_label(GTK_BUTTON(self->priv->author), g_match_info_fetch(match, 1));
					gchar *uri = g_strdup_printf("mailto:%s", g_match_info_fetch(match, 2));
					gtk_link_button_set_uri(self->priv->author, uri);
					g_free(uri);
				}
			}
			else if (g_str_has_prefix(line, COMMITTER_KEY))
			{
				if (g_regex_match(regex, line+strlen(COMMITTER_KEY), 0, &match))
				{
					gtk_button_set_label(GTK_BUTTON(self->priv->committer), g_match_info_fetch(match, 1));
					gchar *uri = g_strdup_printf("mailto:%s", g_match_info_fetch(match, 2));
					gtk_link_button_set_uri(self->priv->committer, uri);
					g_free(uri);
				}
			}
			line = NULL;
		}
		if (line != NULL)
		{
#define SIGNED_OFF_BY_KEY "Signed-off-by: "
			/* We keep only empty lines and lines with whitespace at begining */
			/* This line is part of the log message */
			if (first_line)
				gtk_text_buffer_insert_with_tags(buf, &iter, line, -1, tag_title, NULL);
			else if (g_str_has_prefix(line, SIGNED_OFF_BY_KEY))
				gtk_text_buffer_insert_with_tags(buf, &iter, line, -1, tag_signed, NULL);
			else
				gtk_text_buffer_insert(buf, &iter, line, -1);
			gtk_text_buffer_insert(buf, &iter, "\n", -1);
			first_line = FALSE;
		}
	}
}

static void
gitg_revision_view_init(GitgRevisionView *self)
{
	self->priv = GITG_REVISION_VIEW_GET_PRIVATE(self);
	
	self->priv->log_runner = gitg_runner_new(2000);
	
	g_signal_connect(self->priv->log_runner, "begin-loading", G_CALLBACK(on_log_begin_loading), self);
	g_signal_connect(self->priv->log_runner, "update", G_CALLBACK(on_log_update), self);
	g_signal_connect(self->priv->log_runner, "end-loading", G_CALLBACK(on_log_end_loading), self);

}

#define HASH_KEY "GitgRevisionViewHashKey"

static gboolean
on_parent_clicked(GtkWidget *ev, GdkEventButton *event, gpointer userdata)
{
	if (event->button != 1)
		return FALSE;
	
	GitgRevisionView *rvv = GITG_REVISION_VIEW(userdata);
	
	gchar *hash = (gchar *)g_object_get_data(G_OBJECT(ev), HASH_KEY);
	g_signal_emit(rvv, signals[PARENT_ACTIVATED], 0, hash);

	return FALSE;
}

static GtkWidget *
make_parent_label(GitgRevisionView *self, gchar const *hash)
{
	GtkWidget *ev = gtk_event_box_new();
	GtkWidget *lbl = gtk_label_new(NULL);
	
	gchar *markup = g_strconcat("<span underline='single' foreground='#00f'>", hash, "</span>", NULL);
	gtk_label_set_markup(GTK_LABEL(lbl), markup);
	g_free(markup);

	gtk_misc_set_alignment(GTK_MISC(lbl), 0.0, 0.5);
	gtk_container_add(GTK_CONTAINER(ev), lbl);
	
	gtk_widget_show(ev);
	gtk_widget_show(lbl);
	
	g_object_set_data_full(G_OBJECT(ev), HASH_KEY, (gpointer)gitg_utils_sha1_to_hash_new(hash), (GDestroyNotify)g_free);
	g_signal_connect(ev, "button-release-event", G_CALLBACK(on_parent_clicked), self);

	return ev;
}

static void
update_parents(GitgRevisionView *self, GitgRevision *revision)
{
	GList *children = gtk_container_get_children(GTK_CONTAINER(self->priv->parents));
	GList *item;
	
	for (item = children; item; item = item->next)
		gtk_container_remove(GTK_CONTAINER(self->priv->parents), GTK_WIDGET(item->data));
	
	g_list_free(children);
	
	if (!revision)
		return;
	
	gchar **parents = gitg_revision_get_parents(revision);
	gint num = g_strv_length(parents);
	gint i;
	
	gtk_table_resize(self->priv->parents, num ? num : num + 1, 2);
	GdkCursor *cursor = gdk_cursor_new(GDK_HAND1);
	Hash hash;
	
	for (i = 0; i < num; ++i)
	{
		GtkWidget *widget = make_parent_label(self, parents[i]);
		gtk_table_attach(self->priv->parents, widget, 0, 1, i, i + 1, GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 0, 0);
		
		gtk_widget_realize(widget);
		gdk_window_set_cursor(widget->window, cursor);
		
		/* find subject */
		gitg_utils_sha1_to_hash(parents[i], hash);
		
		GitgRevision *revision = gitg_repository_lookup(self->priv->repository, hash);
		
		if (revision)
		{
			GtkWidget *subject = gtk_label_new(NULL);

			gchar *escaped = g_markup_escape_text(gitg_revision_get_subject(revision), -1);
			gchar *text = g_strdup_printf("(<i>%s</i>)", escaped);
			
			gtk_label_set_markup(GTK_LABEL(subject), text);
			
			g_free(escaped);
			g_free(text);
			
			gtk_widget_show(subject);

			gtk_misc_set_alignment(GTK_MISC(subject), 0.0, 0.5);
			gtk_label_set_ellipsize(GTK_LABEL(subject), PANGO_ELLIPSIZE_MIDDLE);
			gtk_label_set_single_line_mode(GTK_LABEL(subject), TRUE);
			
			gtk_table_attach(self->priv->parents, subject, 1, 2, i, i + 1, GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_SHRINK, 0, 0);
		}
	}

	gdk_cursor_unref(cursor);	
	g_strfreev(parents);	
}

static void
update_log(GitgRevisionView *self, GitgRevision *revision)
{	
	// First cancel a possibly still running log
	gitg_runner_cancel(self->priv->log_runner);
	
	free_cached_headers(self);
	
	// Clear the buffer
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->log));
	gtk_text_buffer_set_text(buffer, "", 0);
	
	if (!revision)
		return;
	
	gchar sign = gitg_revision_get_sign(revision);
	if (sign != 't' && sign != 'u')
	{
		gchar *hash = gitg_revision_get_sha1(revision);
		/* log_runner needs revision */
		self->priv->revision = revision;
		gitg_repository_run_commandv(self->priv->repository, self->priv->log_runner, NULL,
									 "show", "--raw", "-M", "--pretty=full", 
									 "--encoding=UTF-8", hash, NULL);

		g_free(hash);
	}
}

void
gitg_revision_view_update(GitgRevisionView *self, GitgRepository *repository, GitgRevision *revision)
{
	GtkClipboard *cb;
	GtkTextBuffer *tb;
	char sign = ' ';

	g_return_if_fail(GITG_IS_REVISION_VIEW(self));

	tb = gtk_text_view_get_buffer(self->priv->log);

	// Update labels
	if (revision)
		sign = gitg_revision_get_sign(revision);
	if (revision && sign != 't' && sign != 'u')
	{
		gchar *s = g_markup_escape_text(gitg_revision_get_subject(revision), -1);
		gtk_text_buffer_set_text(tb, s, -1);
		g_free(s);

		gchar *date = gitg_utils_timestamp_to_str(gitg_revision_get_timestamp(revision));
		gtk_label_set_text(self->priv->date, date);
		g_free(date);
	
		gchar *sha = gitg_revision_get_sha1(revision);
		gtk_label_set_text(self->priv->sha, sha);

		cb = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		gtk_clipboard_set_text(cb, sha, -1);

		g_free(sha);
	}
	else
	{
		gtk_button_set_label(GTK_BUTTON(self->priv->author), "");
		gtk_link_button_set_uri(self->priv->author, "");
		gtk_button_set_label(GTK_BUTTON(self->priv->committer), "");
		gtk_link_button_set_uri(self->priv->committer, "");
		gtk_text_buffer_set_text(tb, "", -1);
		gtk_label_set_text(self->priv->date, "");
		gtk_label_set_text(self->priv->sha, "");
	}
	
	// Update parents
	update_parents(self, revision);
	
	// Update log
	update_log(self, revision);
}

void 
gitg_revision_view_set_repository(GitgRevisionView *view, GitgRepository *repository)
{
	g_return_if_fail(GITG_IS_REVISION_VIEW(view));
	g_return_if_fail(repository == NULL || GITG_IS_REPOSITORY(repository));

	if (view->priv->repository)
	{
		g_object_unref(view->priv->repository);
		view->priv->repository = NULL;
	}
	
	if (repository)
		view->priv->repository = g_object_ref(repository);
	
	g_object_notify(G_OBJECT(view), "repository");
}
