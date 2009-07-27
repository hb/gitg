/* gitg-command.c
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

#include <string.h> // memmove

#include "gitg-command.h"
#include "gitg-debug.h"

G_DEFINE_TYPE(GitgCommand, gitg_command, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_WORKING_DIRECTORY,
	PROP_ARGUMENTS,
	PROP_ENVIRONMENT,
	PROP_INHERIT_ENVIRONMENT,
};

struct _GitgCommandPrivate
{
	gchar     *working_directory;
	gchar    **arguments;
	gchar    **environment;
	gboolean   inherit_environment;
};

static void
gitg_command_get_property(GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	switch (property_id)
	{
		case PROP_WORKING_DIRECTORY:
			g_value_set_string(value, GITG_COMMAND(object)->priv->working_directory);
			break;
		case PROP_ARGUMENTS:
			g_value_set_boxed(value, g_strdupv(GITG_COMMAND(object)->priv->arguments));
			break;
		case PROP_ENVIRONMENT:
			g_value_set_boxed(value, g_strdupv(GITG_COMMAND(object)->priv->environment));
			break;
		case PROP_INHERIT_ENVIRONMENT:
			g_value_set_boolean(value, GITG_COMMAND(object)->priv->inherit_environment);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void
gitg_command_set_property(GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	GitgCommandPrivate *priv = GITG_COMMAND(object)->priv;
	switch (property_id)
	{
		case PROP_WORKING_DIRECTORY:
			g_free(priv->working_directory);
			priv->working_directory = g_value_dup_string(value);
			break;
		case PROP_ARGUMENTS:
			g_strfreev(priv->arguments);
			priv->arguments = g_strdupv(g_value_get_boxed (value));
			break;
		case PROP_ENVIRONMENT:
			g_strfreev(priv->environment);
			priv->environment = g_strdupv(g_value_get_boxed(value));
			break;
		case PROP_INHERIT_ENVIRONMENT:
			priv->inherit_environment = g_value_get_boolean(value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
	}
}

static void
gitg_command_finalize(GObject *object)
{
	GitgCommand *command = GITG_COMMAND(object);
	
	g_free(command->priv->working_directory);
	command->priv->working_directory = NULL;
	g_strfreev(command->priv->arguments);
	command->priv->arguments = NULL;
	g_strfreev(command->priv->environment);
	command->priv->environment = NULL;

	G_OBJECT_CLASS(gitg_command_parent_class)->finalize(object);
}

static void
gitg_command_dispose(GObject *object)
{
}

static void
gitg_command_class_init(GitgCommandClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	object_class->set_property = gitg_command_set_property;
	object_class->get_property = gitg_command_get_property;
	object_class->finalize     = gitg_command_finalize;
	object_class->dispose      = gitg_command_dispose;

	/**
	 * GitgCommand:working-directory:
	 *
	 * The "working-directory" property.
	 */
	g_object_class_install_property(object_class,
	                                PROP_WORKING_DIRECTORY,
	                                g_param_spec_string("working-directory",
	                                                    "Working directory",
	                                                    "The child's current working directory, or NULL to inherit parent's",
	                                                    NULL,
	                                                    G_PARAM_READWRITE));

	/**
	 * GitgCommand:arguments:
	 *
	 * The "arguments" property.
	 */
	g_object_class_install_property(object_class,
	                                PROP_ARGUMENTS,
	                                g_param_spec_boxed("arguments",
	                                                   "Arguments",
	                                                   "The child's argument vector",
	                                                   G_TYPE_STRV,
	                                                   G_PARAM_CONSTRUCT|G_PARAM_READWRITE));

	/**
	 * GitgCommand:environment:
	 *
	 * The "environment" property.
	 */
	g_object_class_install_property (object_class,
	                                 PROP_ENVIRONMENT,
	                                 g_param_spec_boxed ("environment",
	                                                     "Environment",
	                                                     "The child's environment",
	                                                     G_TYPE_STRV,
	                                                     G_PARAM_READWRITE));


	/**
	 * GitgCommand:inherit_environment:
	 *
	 * The "inherit_environment" property.
	 */
	g_object_class_install_property (object_class,
	                                 PROP_INHERIT_ENVIRONMENT,
	                                 g_param_spec_boolean ("inherit-environment",
	                                                       "Inherit Environment",
	                                                       "Inherit (or not) the parent's environment",
	                                                       TRUE,
	                                                       G_PARAM_READWRITE));

	g_type_class_add_private(object_class, sizeof(GitgCommandPrivate));
}

static void
gitg_command_init(GitgCommand *command)
{
	command->priv = G_TYPE_INSTANCE_GET_PRIVATE(command,
	                                            GITG_TYPE_COMMAND,
	                                            GitgCommandPrivate);
	command->priv->working_directory = NULL;
	command->priv->arguments = NULL;
	command->priv->environment = NULL;
	command->priv->inherit_environment = TRUE;
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


/**
 * gitg_command_new:
 * @arguments: a string vector
 *
 * Return value: newly allocated #GitgCommand
 */
GitgCommand*
gitg_command_new(const gchar **arguments)
{
	return g_object_new(GITG_TYPE_COMMAND, "arguments", arguments, NULL);
}

/**
 * gitg_command_newv:
 * @first: a #gchar*
 *
 * Return value: newly allocated #GitgCommand
 */
GitgCommand*
gitg_command_newv(const gchar *first, ...)
{
	va_list ap;
	va_start(ap, first);
	gchar const *a;
	gchar const **argv = NULL;
	guint num = 0;

	/* First argument */
	argv = g_realloc(argv, sizeof(gchar const *) * (++num + 1));
	argv[num - 1] = first;

	/* Next ones */
	while ((a = va_arg(ap, gchar const *)) != NULL)
	{
		argv = g_realloc(argv, sizeof(gchar const *) * (++num + 1));
		argv[num - 1] = a;
	}

	/* End */
	argv[num] = NULL;
	va_end(ap);
	
	GitgCommand *ret = g_object_new(GITG_TYPE_COMMAND, "arguments", argv, NULL);
	
	g_free(argv);
	
	return ret;
}

/**
 * gitg_command_get_working_directory:
 * @command: A #GitgCommand
 *
 * Return value: 
 */
G_CONST_RETURN gchar*
gitg_command_get_working_directory(GitgCommand *command)
{
	g_return_val_if_fail(GITG_IS_COMMAND(command), NULL);
  
	return command->priv->working_directory;
}

/**
 * gitg_command_set_working_directory:
 * @command: A #GitgCommand
 * @working_directory: A #const gchar
 */
void
gitg_command_set_working_directory(GitgCommand  *command,
                                   const gchar*  working_directory)
{
	g_return_if_fail(GITG_IS_COMMAND(command));
  
	g_free(command->priv->working_directory);
	command->priv->working_directory = NULL;
	
	if (working_directory != NULL)
		command->priv->working_directory = g_strdup(working_directory);
  
	g_object_notify(G_OBJECT(command), "working_directory");
}

/**
 * gitg_command_get_arguments:
 * @command: A #GitgCommand
 *
 * Return value: 
 */
gchar**
gitg_command_get_arguments(GitgCommand *command)
{
	g_return_val_if_fail(GITG_IS_COMMAND(command), NULL);
  
	return command->priv->arguments;
}

/**
 * gitg_command_set_arguments:
 * @command: A #GitgCommand
 * @arguments: A #gpointer
 */
void
gitg_command_set_arguments(GitgCommand  *command,
                           gchar       **arguments)
{
	g_return_if_fail(GITG_IS_COMMAND(command));
  
	g_strfreev(command->priv->arguments);
	command->priv->arguments = NULL;
	
	if (arguments != NULL)
		command->priv->arguments = g_strdupv(arguments);
  
	g_object_notify(G_OBJECT(command), "arguments");
}

/**
 * gitg_command_set_argumentsv:
 * @command: A #GitgCommand
 * @arguments: A NULL terminated enumaration of string
 */
void
gitg_command_set_argumentsv(GitgCommand  *command, ...)
{
	g_return_if_fail(GITG_IS_COMMAND(command));

	va_list ap;
	va_start(ap, command);
	gchar const **argv = parse_valist(ap);
	va_end(ap);
	
	gitg_command_set_arguments(command, argv);
	
	g_free(argv);
}

/**
 * gitg_command_prepend_argument:
 * @command: A #GitgCommand
 * @argument: A #gchar*
 */
void
gitg_command_prepend_argument(GitgCommand  *command,
                              const gchar  *argument)
{
	g_return_if_fail(GITG_IS_COMMAND(command));
	g_return_if_fail(argument != NULL);
	
	guint num = g_strv_length(command->priv->arguments);
	guint i;
  
	/* Realloc array:
     * - add an element (+1)
     * - keep the NULL terminated scheme (+1)
     */
	command->priv->arguments = g_realloc(command->priv->arguments, sizeof(gchar *) * (num + 2));
	
	/* Move elements to free the first index (also copy the NULL: +1) */
	g_memmove(command->priv->arguments+1, command->priv->arguments, sizeof(gchar *) * (num + 1));
  
	/* Duplicate the new element */
	command->priv->arguments[0] = g_strdup(argument);
	
	g_object_notify(G_OBJECT(command), "arguments");
}

/**
 * gitg_command_get_environment:
 * @command: A #GitgCommand
 *
 * Return value: 
 */
gchar**
gitg_command_get_environment (GitgCommand *command)
{
	g_return_val_if_fail(GITG_IS_COMMAND(command), NULL);
	
	return command->priv->environment;
}

/**
 * gitg_command_set_environment:
 * @command: A #GitgCommand
 * @arguments: A #gpointer
 */
void
gitg_command_set_environment (GitgCommand  *command,
                              gchar       **environment)
{
	g_return_if_fail(GITG_IS_COMMAND(command));
	
	g_strfreev(command->priv->environment);
	command->priv->environment = NULL;
	
	if (environment != NULL)
		command->priv->environment = g_strdupv(environment);

	g_object_notify(G_OBJECT(command), "environment");
}

void
gitg_command_add_environment (GitgCommand *command, gchar const *key, gchar const *value)
{
	g_return_if_fail (GITG_IS_COMMAND (command));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	gint len = 0;
	if (command->priv->environment != NULL)
	{
		len = g_strv_length (command->priv->environment);
	}
	
	command->priv->environment = g_realloc (command->priv->environment, 
	                                        sizeof(gchar *) * (len + 2));

	command->priv->environment[len] = g_strconcat (key, "=", value, NULL);
	command->priv->environment[len + 1] = NULL;
}

/**
 * gitg_command_get_inherit_environment:
 * @command: A #GitgCommand
 *
 * Return value: 
 */
gboolean
gitg_command_get_inherit_environment (GitgCommand *command)
{
	g_return_val_if_fail(GITG_IS_COMMAND(command), TRUE);
	
	return command->priv->inherit_environment;
}

/**
 * gitg_command_set_inherit_environment:
 * @command: A #GitgCommand
 * @arguments: A #gboolean
 */
void
gitg_command_set_inherit_environment (GitgCommand *command,
                                      gboolean     inherit)
{
	g_return_if_fail(GITG_IS_COMMAND(command));
	
	command->priv->inherit_environment = inherit;

	g_object_notify(G_OBJECT(command), "inherit-environment");
}

/**
 * gitg_command_spawn_async_with_pipes:
 * @command: A #GitgCommand
 * @arguments: A #gpointer
 */
gboolean
gitg_command_spawn_async_with_pipes(GitgCommand *command,
                                    GPid *child_pid,
                                    gint *standard_input,
                                    gint *standard_output,
                                    gint *standard_error,
                                    GError **error)
{
	g_return_val_if_fail(GITG_IS_COMMAND(command), FALSE);
  
	gchar *wd = command->priv->working_directory;
	gchar **argv = command->priv->arguments;
	gchar **env = command->priv->environment;
	gchar **newenv = NULL;
	
	/* Compute env */
	if (command->priv->inherit_environment == TRUE)
	{
		if (env != NULL)
		{
			int i,j;
			gchar **current = g_listenv();
			newenv = g_malloc0(sizeof(gchar*)*(g_strv_length(current)+g_strv_length(env)+1));
			i=0;
			for(j=0;current[j]!=NULL;j++)
				newenv[i++] = g_strconcat(current[j], "=", g_getenv (current[j]), NULL);
			g_strfreev(current);
			current = NULL;
			for(j=0;env[j]!=NULL;j++)
				newenv[i++] = g_strdup(env[j]);
			env = newenv;
		}
	}
	else
	{
		if (env == NULL)
		{
			newenv = g_malloc0(sizeof(gchar*));
			env = newenv;
		}
	}

	// TODO do we need to rename GITG_DEBUG_RUNNER?
	gboolean ret = g_spawn_async_with_pipes(wd, argv, env, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | (gitg_debug_enabled(GITG_DEBUG_RUNNER) ? 0 : G_SPAWN_STDERR_TO_DEV_NULL), NULL, NULL,
	                                        child_pid, standard_input, standard_output, standard_error, error);
	g_strfreev(newenv);
	return ret;
}
