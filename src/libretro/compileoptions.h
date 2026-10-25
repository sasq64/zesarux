/*
    Fixed build options for the libretro core, in place of the
    compileoptions.h that ./configure writes. Force included, so a configured
    tree's own compileoptions.h is skipped by the shared OPTIONS_H guard.
*/

#ifndef OPTIONS_H
#define OPTIONS_H

#define USE_PTHREADS
#define NETWORKING_DISABLED
#define EMULATE_MEMPTR
#define EMULATE_VISUALMEM
#define EMULATE_CPU_STATS
#define EMULATE_CONTEND
#define PUTPIXELCACHE

#define COMPILATION_DATE __DATE__
#define COMPILATION_SYSTEM "libretro"
#define COMPILATION_SYSTEM_RELEASE ""
#define COMPILATION_MACHINE_HARDWARE_NAME ""
#define CONFIGURE_OPTIONS "Makefile.libretro"
#define COMPILE_VARIABLES " LIBRETRO USE_PTHREADS NETWORKING_DISABLED EMULATE_MEMPTR EMULATE_VISUALMEM EMULATE_CPU_STATS EMULATE_CONTEND PUTPIXELCACHE"
#define COMPILE_INITIALCFLAGS ""
#define COMPILE_INITIALLDFLAGS ""
#define COMPILE_INITIALLIBS ""
#define COMPILE_FINALCFLAGS ""
#define COMPILE_FINALLDFLAGS ""
#define COMPILE_FINALLIBS ""
#define INSTALL_PREFIX "/usr/local"
#define LINES_SOURCE 0
#define TOTAL_C_FILES 0
#define TOTAL_H_FILES 0
#define TOTAL_COMMENTS 0
#define TOTAL_TODO_ITEMS 0
#define BUILDNUMBER "0"

#endif
