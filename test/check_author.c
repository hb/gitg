#include <stdlib.h>

#include "gitg-author.h"

void test()
{
	GitgAuthor *author = gitg_author_new_from_string("User Name <user@email>");
	printf("Name=%s\n", gitg_author_get_name(author));
	printf("Email=%s\n", gitg_author_get_email(author));
	g_object_unref(author);
	author = NULL;
}

int main(int argc, char *argv[])
{
	g_type_init();
	test();
  return EXIT_SUCCESS;
}
