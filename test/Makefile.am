INCLUDES =							\
	-I$(top_srcdir)/gitg						\
	-I$(srcdir)						\
	$(PACKAGE_CFLAGS)					\
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DDATADIR=\""$(datadir)"\"				\
	-DGITG_DATADIR=\""$(datadir)/gitg"\"			\
	-DGITG_LOCALEDIR=\""$(datadir)/locale"\"

check_PROGRAMS = check_author

check_author_SOURCES = check_author.c
check_author_LDADD = $(PACKAGE_LIBS) \
	../gitg/gitg-author.o