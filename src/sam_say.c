/* sam_say: Microsoft Sam text to speech from the original voice data files.
 *
 * usage: sam_say [options] "text" out.wav      ("@file.txt" speaks a UTF-8 text file)
 *   --data DIR      folder containing Sam.spd, LTTS1033.LXA and r1033tts.LXA
 *   --pitch x --speed x --gain x --vibrato depth rate --no-reverse   (see sam.h)
 *   --reverse-units   play every sound unit backwards      --rate HZ   run the vocoder at another rate
 *   --sing            the text is a score: "twin-kle C4 1 C4 1" per line, "- 1" rest, "tempo 100"
 *     --no-smooth  --sing-vibrato CENTS HZ  --transpose SEMITONES
 */
#include "sam.h"
#ifdef SAM_EMBEDDED
#include "sam_data.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *f;
    unsigned samples;
} sink;

static void put(FILE *f, unsigned v, int n)
{
    int i;
    for (i = 0; i < n; i++) fputc((int)((v >> (8 * i)) & 0xFF), f);
}

static unsigned g_rate = 22050;

static void header(FILE *f, unsigned bytes)
{
    fwrite("RIFF", 1, 4, f);
    put(f, 36 + bytes, 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put(f, 16, 4);
    put(f, 1, 2);
    put(f, 1, 2);
    put(f, g_rate, 4);
    put(f, g_rate * 2, 4);
    put(f, 2, 2);
    put(f, 16, 2);
    fwrite("data", 1, 4, f);
    put(f, bytes, 4);
}

static void on_pcm(const int16_t *pcm, size_t n, void *user)
{
    sink *s = user;
    size_t i;
    for (i = 0; i < n; i++) put(s->f, (uint16_t)pcm[i], 2);
    s->samples += (unsigned)n;
}

int main(int argc, char **argv)
{
    const char *dir = NULL, *text = NULL, *out = NULL;
    char spd[1024], lex[1024], lts[1024], err[256];
    sam_params p;
    sam_tts *t;
    sink s;
    int i, sing = 0, no_smooth = 0;
    sam_params_default(&p);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) p.pitch_scale = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) p.speed = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--gain") && i + 1 < argc) p.gain = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--vibrato") && i + 2 < argc) {
            p.vibrato = (float)atof(argv[++i]);
            p.vibrato_rate = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--no-reverse")) p.no_reverse = 1;
        else if (!strcmp(argv[i], "--reverse-units")) p.reverse_units = 1;
        else if (!strcmp(argv[i], "--sing")) sing = 1;
        else if (!strcmp(argv[i], "--smooth")) p.smooth = 1;
        else if (!strcmp(argv[i], "--no-smooth")) no_smooth = 1;
        else if (!strcmp(argv[i], "--sing-vibrato") && i + 2 < argc) {
            p.sing_vibrato = (float)atof(argv[++i]);
            p.sing_vibrato_rate = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--transpose") && i + 1 < argc) p.transpose = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) g_rate = (unsigned)(p.out_rate = atoi(argv[++i]));
        else if (!text) text = argv[i];
        else if (!out) out = argv[i];
    }
    if (sing && !no_smooth) p.smooth = 1; /* singing blends frames unless told not to */
    if (text && text[0] == '@') { /* read the text from a file */
        FILE *f = fopen(text + 1, "rb");
        char *buf;
        long n;
        if (!f) {
            fprintf(stderr, "cannot read %s\n", text + 1);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = malloc((size_t)n + 1);
        if (!buf) return 1;
        n = (long)fread(buf, 1, (size_t)n, f);
        buf[n] = 0;
        fclose(f);
        text = buf;
        if ((unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf) text += 3;
    }
    if (!text || !out) {
        fprintf(stderr, "usage: sam_say [--data DIR] [--pitch x] [--speed x] [--gain x] [--vibrato d r] \"text\" out.wav\n");
        return 1;
    }
#ifdef SAM_EMBEDDED
    if (!dir) /* voice data compiled into the program */
        t = sam_tts_new_mem(sam_data_spd, sam_data_spd_size, sam_data_lex, sam_data_lex_size, sam_data_lts,
                            sam_data_lts_size, &p, err, sizeof err);
    else
#endif
    {
        if (!dir) dir = ".";
        snprintf(spd, sizeof spd, "%s/Sam.spd", dir);
        snprintf(lex, sizeof lex, "%s/LTTS1033.LXA", dir);
        snprintf(lts, sizeof lts, "%s/r1033tts.LXA", dir);
        t = sam_tts_new(spd, lex, lts, &p, err, sizeof err);
    }
    if (!t) {
        fprintf(stderr, "sam_say: %s\n", err);
        return 1;
    }
    s.f = fopen(out, "wb");
    s.samples = 0;
    if (!s.f) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    header(s.f, 0);
    if ((sing ? sam_tts_sing(t, text, on_pcm, &s) : sam_tts_speak(t, text, on_pcm, &s)) != 0) fprintf(stderr, "sam_say: synthesis error\n");
    fseek(s.f, 0, SEEK_SET);
    header(s.f, s.samples * 2);
    fclose(s.f);
    sam_tts_free(t);
    printf("%u samples (%.2f s)\n", s.samples, s.samples / (double)g_rate);
    return 0;
}
