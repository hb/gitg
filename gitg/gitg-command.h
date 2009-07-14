/* gitg-command.h
 * 
 * Copyright (C) 2009 Guilhem Bonnefille <guilhem.bonnefille@gmail.com>
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with This; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef __GITG_COMMAND_H__
#define __GITG_COMMAND_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GITG_TYPE_COMMAND            (gitg_command_get_type())          
#define GITG_COMMAND(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GITG_TYPE_COMMAND, GitgCommand))     
#define GITG_COMMAND_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GITG_TYPE_COMMAND, GitgCommandClass))
#define GITG_IS_COMMAND(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GITG_TYPE_COMMAND))                  
#define GITG_IS_COMMAND_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GITG_TYPE_COMMAND))                  
#define GITG_COMMAND_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GITG_TYPE_COMMAND, GitgCommandClass))

typedef struct _GitgCommand        GitgCommand;
typedef struct _GitgCommandClass   GitgCommandClass;
typedef struct _GitgCommandPrivate GitgCommandPrivate;

struct _GitgCommand
{
	GObject parent;

	/*< private >*/
	GitgCommandPrivate *priv;
};

struct _GitgCommandClass
{
	GObjectClass parent_class;
};

GType        gitg_command_get_type (void);
GitgCommand* gitg_command_new      (const gchar **arguments);
GitgCommand* gitg_command_newv     (const gchar *first, ...) G_GNUC_NULL_TERMINATED;

G_CONST_RETURN gchar* gitg_command_get_working_directory (GitgCommand *command);
void                  gitg_command_set_working_directory (GitgCommand *command, const gchar* working_directory);
gchar**               gitg_command_get_arguments         (GitgCommand *command);
void                  gitg_command_set_arguments         (GitgCommand *command, gchar** arguments);
void                  gitg_command_set_argumentsv        (GitgCommand  *command, ...) G_GNUC_NULL_TERMINATED;
void                  gitg_command_prepend_argument      (GitgCommand *command, const gchar* argument);

gboolean              gitg_command_spawn_async_with_pipes (GitgCommand *command, GPid *child_pid, gint *standard_input, gint *standard_output, gint *standard_error, GError **error);

G_END_DECLS

#endif /* __GITG_COMMAND_H__ */
