/*
 * ThunderSense - funconfig.h  (ch32fun build-time options)
 * Required by ch32fun. Kept minimal.
 */
#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

#define CH32V003                 1
/* SDI printf backend for the debug console (used only when config.h DEBUG=1;
 * unused code is garbage-collected by the linker when DEBUG=0). */
#define FUNCONF_USE_DEBUGPRINTF  1

#endif /* _FUNCONFIG_H */
