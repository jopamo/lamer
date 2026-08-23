#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_NCURSES_TERMCAP_H)
# include <ncurses/termcap.h>
#elif defined(HAVE_TERMCAP_H)
# include <termcap.h>
#elif defined(HAVE_TERMCAP)
# include <curses.h>
# if !defined(__bsdi__)
#  include <term.h>
# endif
#endif

#include "console.h"
#include "main.h"

#ifdef WITH_DMALLOC
# include <dmalloc.h>
#endif

#define CLASS_ID 0x434F4E53UL

#define DEFAULT_CONSOLE_WIDTH  80
#define DEFAULT_CONSOLE_HEIGHT 25


/*
 * Copy a string into a fixed-size destination.
 *
 * Unlike strncpy(), this always terminates the destination when n != 0
 * and does not unnecessarily zero-fill the remainder of the buffer.
 */
static void
copy_string(char *dest, size_t n, const char *src)
{
    size_t len;

    if (dest == NULL || n == 0)
        return;

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= n)
        len = n - 1;

    memcpy(dest, src, len);
    dest[len] = '\0';
}


#ifdef HAVE_TERMCAP

/*
 * Modern ncurses accepts a NULL storage argument to tgetstr().
 *
 * This is important: the traditional termcap API has no way to tell
 * tgetstr() how large a caller-provided output area is.  Passing a tiny
 * temporary buffer therefore cannot be made bounds-safe.
 *
 * LAME copies only the resulting capability into its own bounded
 * destination.
 */
static void
get_termcap_string(const char *id, char *dest, size_t n)
{
    const char *value;

    if (id == NULL || dest == NULL || n == 0)
        return;

    value = tgetstr(id, NULL);
    if (value != NULL)
        copy_string(dest, n, value);
}


static void
get_termcap_number(const char *id, int *dest, int low, int high)
{
    int value;

    if (id == NULL || dest == NULL)
        return;

    value = tgetnum(id);

    if (value >= low && value <= high)
        *dest = value;
}


static void
apply_termcap_settings(Console_IO_t *mfp)
{
    const char *term_name;

    if (mfp == NULL)
        return;

    term_name = getenv("TERM");

    if (term_name == NULL || term_name[0] == '\0')
        return;

    /*
     * ncurses and modern terminfo-backed termcap implementations do not
     * require a caller-owned terminal-description buffer.
     */
    if (tgetent(NULL, term_name) != 1)
        return;

    get_termcap_number("co",
                       &mfp->disp_width,
                       40,
                       512);

    get_termcap_number("li",
                       &mfp->disp_height,
                       16,
                       256);

    get_termcap_string("up",
                       mfp->str_up,
                       sizeof(mfp->str_up));

    get_termcap_string("md",
                       mfp->str_emph,
                       sizeof(mfp->str_emph));

    get_termcap_string("me",
                       mfp->str_norm,
                       sizeof(mfp->str_norm));

    get_termcap_string("ce",
                       mfp->str_clreoln,
                       sizeof(mfp->str_clreoln));
}

#endif /* HAVE_TERMCAP */


static int
is_console_initialized(const Console_IO_t *mfp)
{
    return mfp != NULL && mfp->ClassID == CLASS_ID;
}


static void
reset_console_strings(Console_IO_t *mfp)
{
    if (mfp == NULL)
        return;

    /*
     * Cursor-up has historically had an ANSI fallback.
     * The other capabilities remain empty unless termcap supplies them.
     */
    copy_string(mfp->str_up,
                sizeof(mfp->str_up),
                "\033[A");

    mfp->str_emph[0] = '\0';
    mfp->str_norm[0] = '\0';
    mfp->str_clreoln[0] = '\0';
}


static int
init_console(Console_IO_t *mfp)
{
    if (mfp == NULL)
        return -1;

    if (is_console_initialized(mfp))
        return 0;

    /*
     * Keep the object visibly uninitialized until setup has completed.
     */
    mfp->ClassID = 0;

    mfp->disp_width = DEFAULT_CONSOLE_WIDTH;
    mfp->disp_height = DEFAULT_CONSOLE_HEIGHT;

    mfp->Console_fp = stderr;
    mfp->Error_fp = stderr;
    mfp->Report_fp = NULL;

    reset_console_strings(mfp);

#ifdef HAVE_TERMCAP
    apply_termcap_settings(mfp);
#endif

    /*
     * Do not change stderr's buffering with setvbuf().
     *
     * stderr is process-global, and changing its buffering here affects
     * code outside this module.  It also makes teardown/reinitialization
     * unnecessarily complicated.
     */

    mfp->ClassID = CLASS_ID;

    return 0;
}


static void
deinit_console(Console_IO_t *mfp)
{
    FILE *console_fp;
    FILE *error_fp;

    if (!is_console_initialized(mfp))
        return;

    console_fp = mfp->Console_fp;
    error_fp = mfp->Error_fp;

    /*
     * fclose() flushes Report_fp.
     */
    if (mfp->Report_fp != NULL) {
        fclose(mfp->Report_fp);
        mfp->Report_fp = NULL;
    }

    if (console_fp != NULL)
        (void) fflush(console_fp);

    /*
     * Avoid flushing the same FILE twice when both channels use stderr.
     */
    if (error_fp != NULL && error_fp != console_fp)
        (void) fflush(error_fp);

    /*
     * Invalidate the object completely.  In particular, ClassID must be
     * cleared so frontend_open_console() can initialize it again.
     */
    mfp->Console_fp = NULL;
    mfp->Error_fp = NULL;

    mfp->disp_width = DEFAULT_CONSOLE_WIDTH;
    mfp->disp_height = DEFAULT_CONSOLE_HEIGHT;

    mfp->str_up[0] = '\0';
    mfp->str_emph[0] = '\0';
    mfp->str_norm[0] = '\0';
    mfp->str_clreoln[0] = '\0';

    mfp->ClassID = 0;
}


/*
 * LAME console state.
 *
 * Static-duration objects are zero-initialized before program startup.
 */
Console_IO_t Console_IO;


enum ConsoleEnum {
    ConsoleIoConsole,
    ConsoleIoError,
    ConsoleIoReport
};


static FILE *
frontend_console_file_handle(enum ConsoleEnum channel)
{
    if (!is_console_initialized(&Console_IO))
        return NULL;

    switch (channel) {
    case ConsoleIoConsole:
        return Console_IO.Console_fp;

    case ConsoleIoError:
        return Console_IO.Error_fp;

    case ConsoleIoReport:
        return Console_IO.Report_fp;

    default:
        return NULL;
    }
}


static int
frontend_console_print(const char *format,
                       va_list ap,
                       enum ConsoleEnum channel)
{
    FILE *fp;

    if (format == NULL)
        return 0;

    fp = frontend_console_file_handle(channel);

    if (fp == NULL)
        return 0;

    return vfprintf(fp, format, ap);
}


static void
frontend_console_flush(enum ConsoleEnum channel)
{
    FILE *fp;

    fp = frontend_console_file_handle(channel);

    if (fp != NULL)
        (void) fflush(fp);
}


int
frontend_open_console(void)
{
    return init_console(&Console_IO);
}


void
frontend_close_console(void)
{
    deinit_console(&Console_IO);
}


void
frontend_debugf(const char *format, va_list ap)
{
    (void) frontend_console_print(format,
                                  ap,
                                  ConsoleIoReport);
}


void
frontend_msgf(const char *format, va_list ap)
{
    (void) frontend_console_print(format,
                                  ap,
                                  ConsoleIoConsole);
}


void
frontend_errorf(const char *format, va_list ap)
{
    (void) frontend_console_print(format,
                                  ap,
                                  ConsoleIoError);
}


void
frontend_print_null(const char *format, va_list ap)
{
    (void) format;
    (void) ap;
}


int
console_printf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = frontend_console_print(format,
                                 ap,
                                 ConsoleIoConsole);
    va_end(ap);

    return ret;
}


int
error_printf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = frontend_console_print(format,
                                 ap,
                                 ConsoleIoError);
    va_end(ap);

    return ret;
}


int
report_printf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = frontend_console_print(format,
                                 ap,
                                 ConsoleIoReport);
    va_end(ap);

    return ret;
}


void
console_flush(void)
{
    frontend_console_flush(ConsoleIoConsole);
}


void
error_flush(void)
{
    frontend_console_flush(ConsoleIoError);
}


void
report_flush(void)
{
    frontend_console_flush(ConsoleIoReport);
}


void
console_up(int n_lines)
{
    FILE *fp;

    if (n_lines <= 0)
        return;

    if (!is_console_initialized(&Console_IO))
        return;

    fp = Console_IO.Console_fp;

    if (fp == NULL)
        return;

    while (n_lines-- > 0)
        (void) fputs(Console_IO.str_up, fp);

    (void) fflush(fp);
}


int
console_getwidth(void)
{
    if (is_console_initialized(&Console_IO))
        return Console_IO.disp_width;

    return DEFAULT_CONSOLE_WIDTH;
}


void
set_debug_file(const char *fn)
{
    FILE *fp;

    if (!is_console_initialized(&Console_IO))
        return;

    if (Console_IO.Report_fp != NULL)
        return;

    if (fn == NULL || fn[0] == '\0') {
        error_printf("Error: no debug output filename specified\n");
        return;
    }

    fp = lame_fopen(fn, "a");

    if (fp == NULL) {
        error_printf("Error: can't open for debug info: %s\n", fn);
        return;
    }

    Console_IO.Report_fp = fp;

    error_printf("writing debug info into: %s\n", fn);
}

/* end of console.c */
