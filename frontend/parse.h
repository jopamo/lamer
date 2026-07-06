#ifndef PARSE_H_INCLUDED
#define PARSE_H_INCLUDED

#if defined(__cplusplus)
extern "C" {
#endif

int     parse_args(lame_global_flags * gfp, int argc, char **argv, char *const inPath,
                   char *const outPath, char **nogap_inPath, int *num_nogap);

void    parse_close();

#if defined(__cplusplus)
}
#endif

#endif
/* end of parse.h */
