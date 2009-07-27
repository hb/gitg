#include <stdlib.h>
#include <stdio.h>

#include "gitg-command.h"
#include "gitg-runner.h"

GMainLoop *loop = NULL;
int test_id = 0;

void run_next_test();

void end_loading (GitgRunner *runner, gboolean cancelled)
{
	/* Previous test finished: next */
	run_next_test();
}

void updatefunc (GitgRunner *runner, gchar **buffer)
{
	gchar *iter;
	int i;
	for (i=0, iter = buffer[i] ; iter ; i++, iter=buffer[i])
		printf("[%X] %s\n", runner, iter);
}

void test_env(gboolean setenv, gboolean inherit)
{
	g_debug("test_env(%d,%d)",setenv,inherit);
	GError *error = NULL;
	gchar *args[] = { "env", NULL };
	gchar *env[] = { "GITG_FIRST=first", "GITG_SECOND=second", NULL };
	GitgCommand *command = g_object_new (GITG_TYPE_COMMAND,
	                                     "arguments", args,
	                                     NULL);
	if (setenv)
		gitg_command_set_environment(command, env);
	if (inherit)
		gitg_command_set_inherit_environment(command, TRUE);
	else
		gitg_command_set_inherit_environment(command, FALSE);
	GitgRunner *runner = gitg_runner_new_synchronized(255);
	g_signal_connect(runner, "update", updatefunc, NULL);
	g_signal_connect(runner, "end-loading", end_loading, NULL);
	gitg_runner_run_command(runner, command, NULL, &error);
}

void test_empty()
{
	test_env(FALSE,FALSE);
}

void test_simple()
{
	test_env(FALSE,TRUE);
}

void test_no_inherit()
{
	test_env(TRUE,FALSE);
}

void test_inherit()
{
	test_env(TRUE,TRUE);
}

void end()
{
	g_main_loop_quit(loop);
}

typedef void (*test_function_t)(void);

test_function_t test_serie[] = {
test_empty,
test_simple,
test_no_inherit,
test_inherit,
end,
NULL
};

void
run_next_test()
{
	test_function_t func = test_serie[test_id];
	test_id++;
	if (func != NULL) func();
}

int
main(int argc, char *argv[])
{
	loop = g_main_loop_new(NULL, FALSE);
	g_type_init();
	run_next_test();
	g_main_loop_run(loop);
	return EXIT_SUCCESS;
}
