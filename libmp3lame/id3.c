/*
 * Cold-path ID3 compatibility API.
 *
 * The encoder itself does not write tags automatically.  These entry points
 * keep the public LAME API usable by applications such as libsndfile, which
 * assemble ID3v2 data before encoding and append ID3v1 data after flushing.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lame.h"
#include "machine.h"
#include "encoder.h"
#include "lame_global_flags.h"
#include "util.h"

#define ID3_TEXT_MAX 1023

struct lamer_id3_state {
    char title[ID3_TEXT_MAX + 1];
    char artist[ID3_TEXT_MAX + 1];
    char album[ID3_TEXT_MAX + 1];
    char year[ID3_TEXT_MAX + 1];
    char comment[ID3_TEXT_MAX + 1];
    char track[ID3_TEXT_MAX + 1];
    char genre[ID3_TEXT_MAX + 1];
};

static char const* const id3_genres[] = {"Blues",
                                         "Classic Rock",
                                         "Country",
                                         "Dance",
                                         "Disco",
                                         "Funk",
                                         "Grunge",
                                         "Hip-Hop",
                                         "Jazz",
                                         "Metal",
                                         "New Age",
                                         "Oldies",
                                         "Other",
                                         "Pop",
                                         "R&B",
                                         "Rap",
                                         "Reggae",
                                         "Rock",
                                         "Techno",
                                         "Industrial",
                                         "Alternative",
                                         "Ska",
                                         "Death Metal",
                                         "Pranks",
                                         "Soundtrack",
                                         "Euro-Techno",
                                         "Ambient",
                                         "Trip-Hop",
                                         "Vocal",
                                         "Jazz+Funk",
                                         "Fusion",
                                         "Trance",
                                         "Classical",
                                         "Instrumental",
                                         "Acid",
                                         "House",
                                         "Game",
                                         "Sound Clip",
                                         "Gospel",
                                         "Noise",
                                         "AlternRock",
                                         "Bass",
                                         "Soul",
                                         "Punk",
                                         "Space",
                                         "Meditative",
                                         "Instrumental Pop",
                                         "Instrumental Rock",
                                         "Ethnic",
                                         "Gothic",
                                         "Darkwave",
                                         "Techno-Industrial",
                                         "Electronic",
                                         "Pop-Folk",
                                         "Eurodance",
                                         "Dream",
                                         "Southern Rock",
                                         "Comedy",
                                         "Cult",
                                         "Gangsta",
                                         "Top 40",
                                         "Christian Rap",
                                         "Pop/Funk",
                                         "Jungle",
                                         "Native American",
                                         "Cabaret",
                                         "New Wave",
                                         "Psychadelic",
                                         "Rave",
                                         "Showtunes",
                                         "Trailer",
                                         "Lo-Fi",
                                         "Tribal",
                                         "Acid Punk",
                                         "Acid Jazz",
                                         "Polka",
                                         "Retro",
                                         "Musical",
                                         "Rock & Roll",
                                         "Hard Rock",
                                         "Folk",
                                         "Folk-Rock",
                                         "National Folk",
                                         "Swing",
                                         "Fast Fusion",
                                         "Bebob",
                                         "Latin",
                                         "Revival",
                                         "Celtic",
                                         "Bluegrass",
                                         "Avantgarde",
                                         "Gothic Rock",
                                         "Progressive Rock",
                                         "Psychedelic Rock",
                                         "Symphonic Rock",
                                         "Slow Rock",
                                         "Big Band",
                                         "Chorus",
                                         "Easy Listening",
                                         "Acoustic",
                                         "Humour",
                                         "Speech",
                                         "Chanson",
                                         "Opera",
                                         "Chamber Music",
                                         "Sonata",
                                         "Symphony",
                                         "Booty Bass",
                                         "Primus",
                                         "Porn Groove",
                                         "Satire",
                                         "Slow Jam",
                                         "Club",
                                         "Tango",
                                         "Samba",
                                         "Folklore",
                                         "Ballad",
                                         "Power Ballad",
                                         "Rhythmic Soul",
                                         "Freestyle",
                                         "Duet",
                                         "Punk Rock",
                                         "Drum Solo",
                                         "A capella",
                                         "Euro-House",
                                         "Dance Hall",
                                         "Goa",
                                         "Drum & Bass",
                                         "Club-House",
                                         "Hardcore",
                                         "Terror",
                                         "Indie",
                                         "BritPop",
                                         "Negerpunk",
                                         "Polsk Punk",
                                         "Beat",
                                         "Christian Gangsta Rap",
                                         "Heavy Metal",
                                         "Black Metal",
                                         "Crossover",
                                         "Contemporary Christian",
                                         "Christian Rock",
                                         "Merengue",
                                         "Salsa",
                                         "Thrash Metal",
                                         "Anime",
                                         "JPop",
                                         "Synthpop"};

void id3tag_genre_list(void (*const handler)(int, char const*, void*), void* const cookie) {
    size_t i;

    if (handler == NULL)
        return;
    for (i = 0; i < sizeof(id3_genres) / sizeof(id3_genres[0]); ++i)
        handler((int)i, id3_genres[i], cookie);
}

static struct lamer_id3_state* id3_state(lame_t const gfp, int create) {
    lame_internal_flags* gfc;

    if (!is_lame_global_flags_valid(gfp) || gfp->internal_flags == NULL)
        return NULL;
    gfc = gfp->internal_flags;
    if (gfc->id3 == NULL && create)
        gfc->id3 = calloc(1, sizeof(*gfc->id3));
    return gfc->id3;
}

static void copy_text(char* const dst, size_t const dst_size, char const* src) {
    size_t length;

    if (src == NULL)
        src = "";
    length = strlen(src);
    if (length >= dst_size)
        length = dst_size - 1;
    memcpy(dst, src, length);
    dst[length] = '\0';
}

static int has_text(char const* const text) {
    return text != NULL && text[0] != '\0';
}

static int has_any_text(struct lamer_id3_state const* const tag) {
    return has_text(tag->title) || has_text(tag->artist) || has_text(tag->album) || has_text(tag->year) || has_text(tag->comment) || has_text(tag->track) || has_text(tag->genre);
}

static size_t text_frame_size(char const* const text) {
    return has_text(text) ? 10 + 1 + strlen(text) : 0;
}

static size_t comment_frame_size(char const* const text) {
    return has_text(text) ? 10 + 1 + 3 + 1 + strlen(text) : 0;
}

static void write_syncsafe(unsigned char* const dst, size_t value) {
    dst[0] = (unsigned char)((value >> 21) & 0x7f);
    dst[1] = (unsigned char)((value >> 14) & 0x7f);
    dst[2] = (unsigned char)((value >> 7) & 0x7f);
    dst[3] = (unsigned char)(value & 0x7f);
}

static size_t append_text_frame(unsigned char* const dst, size_t offset, char const id[4], char const* const text) {
    size_t const text_size = has_text(text) ? strlen(text) : 0;
    size_t const payload_size = 1 + text_size;

    if (text_size == 0)
        return offset;
    memcpy(dst + offset, id, 4);
    write_syncsafe(dst + offset + 4, payload_size);
    dst[offset + 8] = 0;
    dst[offset + 9] = 0;
    dst[offset + 10] = 3; /* UTF-8 */
    memcpy(dst + offset + 11, text, text_size);
    return offset + 10 + payload_size;
}

static size_t append_comment_frame(unsigned char* const dst, size_t offset, char const* const text) {
    size_t const text_size = has_text(text) ? strlen(text) : 0;
    size_t const payload_size = 1 + 3 + 1 + text_size;

    if (text_size == 0)
        return offset;
    memcpy(dst + offset, "COMM", 4);
    write_syncsafe(dst + offset + 4, payload_size);
    dst[offset + 8] = 0;
    dst[offset + 9] = 0;
    dst[offset + 10] = 3; /* UTF-8 */
    memcpy(dst + offset + 11, "eng", 3);
    dst[offset + 14] = 0; /* empty description */
    memcpy(dst + offset + 15, text, text_size);
    return offset + 10 + payload_size;
}

static int parse_byte_value(char const* const text) {
    char* end;
    long value;

    if (!has_text(text))
        return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < 0 || value > UCHAR_MAX)
        return -1;
    return (int)value;
}

static void copy_id3v1_field(unsigned char* const dst, size_t const size, char const* const text) {
    size_t length = has_text(text) ? strlen(text) : 0;

    if (length > size)
        length = size;
    memset(dst, 0, size);
    memcpy(dst, text, length);
}

void lame_set_write_id3tag_automatic(lame_global_flags* const gfp, int const enabled) {
    /*
     * Lamer never inserts ID3 data into the audio stream automatically.
     * Keep the call source-compatible; callers that need tags use the
     * explicit get_*_tag APIs below.
     */
    (void)gfp;
    (void)enabled;
}

void id3tag_init(lame_t const gfp) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);

    if (tag != NULL)
        memset(tag, 0, sizeof(*tag));
}

void id3tag_set_title(lame_t const gfp, char const* const value) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);
    if (tag != NULL)
        copy_text(tag->title, sizeof(tag->title), value);
}

void id3tag_set_artist(lame_t const gfp, char const* const value) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);
    if (tag != NULL)
        copy_text(tag->artist, sizeof(tag->artist), value);
}

void id3tag_set_album(lame_t const gfp, char const* const value) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);
    if (tag != NULL)
        copy_text(tag->album, sizeof(tag->album), value);
}

void id3tag_set_year(lame_t const gfp, char const* const value) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);
    if (tag != NULL)
        copy_text(tag->year, sizeof(tag->year), value);
}

void id3tag_set_comment(lame_t const gfp, char const* const value) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);
    if (tag != NULL)
        copy_text(tag->comment, sizeof(tag->comment), value);
}

int id3tag_set_track(lame_t const gfp, char const* const value) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);
    if (tag == NULL)
        return -1;
    copy_text(tag->track, sizeof(tag->track), value);
    return 0;
}

int id3tag_set_genre(lame_t const gfp, char const* const value) {
    struct lamer_id3_state* const tag = id3_state(gfp, 1);
    if (tag == NULL)
        return -1;
    copy_text(tag->genre, sizeof(tag->genre), value);
    return 0;
}

size_t lame_get_id3v1_tag(lame_t const gfp, unsigned char* const buffer, size_t const size) {
    struct lamer_id3_state const* const tag = id3_state(gfp, 0);
    int const track = tag != NULL ? parse_byte_value(tag->track) : -1;
    int const genre = tag != NULL ? parse_byte_value(tag->genre) : -1;

    if (tag == NULL || !has_any_text(tag))
        return 0;
    if (buffer == NULL || size < 128)
        return 128;

    memset(buffer, 0, 128);
    memcpy(buffer, "TAG", 3);
    copy_id3v1_field(buffer + 3, 30, tag->title);
    copy_id3v1_field(buffer + 33, 30, tag->artist);
    copy_id3v1_field(buffer + 63, 30, tag->album);
    copy_id3v1_field(buffer + 93, 4, tag->year);
    copy_id3v1_field(buffer + 97, track >= 0 ? 28 : 30, tag->comment);
    if (track >= 0) {
        buffer[125] = 0;
        buffer[126] = (unsigned char)track;
    }
    buffer[127] = genre >= 0 ? (unsigned char)genre : 255;
    return 128;
}

size_t lame_get_id3v2_tag(lame_t const gfp, unsigned char* const buffer, size_t const size) {
    struct lamer_id3_state const* const tag = id3_state(gfp, 0);
    size_t body_size;
    size_t offset;

    if (tag == NULL || !has_any_text(tag))
        return 0;
    body_size = text_frame_size(tag->title) + text_frame_size(tag->artist) + text_frame_size(tag->album) + text_frame_size(tag->year) + comment_frame_size(tag->comment) + text_frame_size(tag->track) +
                text_frame_size(tag->genre);
    if (buffer == NULL || size < body_size + 10)
        return body_size + 10;

    memcpy(buffer, "ID3", 3);
    buffer[3] = 4; /* ID3v2.4 */
    buffer[4] = 0;
    buffer[5] = 0;
    write_syncsafe(buffer + 6, body_size);
    offset = 10;
    offset = append_text_frame(buffer, offset, "TIT2", tag->title);
    offset = append_text_frame(buffer, offset, "TPE1", tag->artist);
    offset = append_text_frame(buffer, offset, "TALB", tag->album);
    offset = append_text_frame(buffer, offset, "TDRC", tag->year);
    offset = append_comment_frame(buffer, offset, tag->comment);
    offset = append_text_frame(buffer, offset, "TRCK", tag->track);
    offset = append_text_frame(buffer, offset, "TCON", tag->genre);
    assert(offset == body_size + 10);
    return offset;
}

size_t lame_get_lametag_frame(const lame_global_flags* const gfp, unsigned char* const buffer, size_t const size) {
    /*
     * The current encoder does not reserve or generate a Xing/LAME info
     * frame.  Returning zero is the documented "not available" result and
     * lets callers safely skip the optional seek-back operation.
     */
    (void)gfp;
    (void)buffer;
    (void)size;
    return 0;
}
