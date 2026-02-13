/* config.h for ImposOS doomgeneric port */

#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1

/* We do NOT have these on ImposOS */
#undef HAVE_UNISTD_H
#undef HAVE_SYS_STAT_H
#undef HAVE_SYS_TYPES_H
#undef HAVE_MMAP
#undef HAVE_LIBM
#undef HAVE_LIBPNG
#undef HAVE_LIBZ
#undef HAVE_LIBSAMPLERATE
#undef HAVE_MEMORY_H
#undef ORIGCODE

#define PACKAGE "Doom"
#define PACKAGE_NAME "Doom Generic"
#define PACKAGE_STRING "Doom Generic 0.1"
#define PACKAGE_TARNAME "doomgeneric.tar"
#define PACKAGE_URL ""
#define PACKAGE_VERSION 0.1
#define PROGRAM_PREFIX "doomgeneric"
#define STDC_HEADERS 1
#define VERSION 0.1
#define FILES_DIR "."
