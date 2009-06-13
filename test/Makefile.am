INCLUDES =							\
	-I$(top_srcdir)/gitg						\
	-I$(srcdir)						\
	$(PACKAGE_CFLAGS)					\
	$(WARN_CFLAGS)						\
	$(DISABLE_DEPRECATED_CFLAGS)				\
	-DDATADIR=\""$(datadir)"\"				\
	-DGITG_DATADIR=\""$(datadir)/gitg"\"			\
	-DGITG_LOCALEDIR=\""$(datadir)/locale"\"

check_PROGRAMS = check_command

check_command_SOURCES = check_command.c
check_command_LDADD = $(PACKAGE_LIBS) \
	../gitg/gitg-runner.o \
	../gitg/gitg-command.o \
	../gitg/gitg-debug.o \
	../gitg/gitg-utils.o \
	../gitg/gitg-repository.o \
	../gitg/gitg-revision.o \
	../gitg/gitg-preferences.o \
	../gitg/gitg-lane.o \
	../gitg/gitg-lanes.o \
	../gitg/gitg-ref.o \
	../gitg/gitg-data-binding.o \
	../gitg/gitg-dirs.o \
	../gitg/gitg-color.o 

