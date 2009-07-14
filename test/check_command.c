#include <stdlib.h>

#include "gitg-command.h"
#include "gitg-runner.h"

void updatefunc (GitgRunner *runner, gchar **buffer)
{
	gchar *iter;
	int i;
	for (i=0, iter = buffer[i] ; iter ; i++, iter=buffer[i])
	printf("%s\n", iter);
}

void test()
{
	GError *error = NULL;
	gchar *args[] = { "env", NULL };
	GitgCommand *command = g_object_new(GITG_TYPE_COMMAND, "arguments", args, NULL);
	GitgRunner *runner = gitg_runner_new_synchronized(255);
	g_signal_connect(runner, "update", updatefunc, NULL);
	gitg_runner_run_command(runner, command, NULL, &error);
}

int main(int argc, char *argv[])
{
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_type_init();
	test();
	g_main_loop_run(loop);
	return EXIT_SUCCESS;
}
