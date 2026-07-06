#ifndef LAME_USAGE_H
#define LAME_USAGE_H

#include <stdio.h>

#include "lame.h"

#if defined(__cplusplus)
extern "C" {
#endif

int usage(FILE * const fp, const char *ProgramName);
int short_help(const lame_global_flags * gfp, FILE * const fp, const char *ProgramName);
int long_help(const lame_global_flags * gfp, FILE * const fp, const char *ProgramName,
              int lessmode);
int display_bitrates(FILE * const fp);

int frontend_version_print(FILE * const fp);
int frontend_print_license(FILE * const fp);
void frontend_help_id3tag(FILE * const fp);
void frontend_help_developer_switches(FILE * const fp);
void frontend_presets_longinfo(FILE * const fp);

#if defined(__cplusplus)
}
#endif

#endif
