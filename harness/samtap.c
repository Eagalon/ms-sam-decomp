/* samtap: drive the real Microsoft Sam (SAPI5 spttseng.dll) and log its internals.
 *
 * 32-bit only. Loads the installed engine DLL, hot-patches internal functions, speaks the
 * given text into a 22050 Hz WAV, and writes a binary log of tagged records:
 *   u32 tag, u32 byte_len, payload
 * Build: cl /nologo /O2 /W3 samtap.c ole32.lib sapi.lib   (x86 environment)
 * Usage: samtap.exe "text" out.wav out.log
 */
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <sapi.h>
#include <stdio.h>
#include <string.h>

#define ENGINE_PATH L"C:\\Program Files (x86)\\Common Files\\SpeechEngines\\Microsoft\\TTS\\1033\\spttseng.dll"
#define SAM_TOKEN L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\MSSam"
#define IMAGE_BASE 0x5ed30000u

/* log record tags */
enum {
    T_UNIT = 1,      /* i32 unit index, i32 nfr, i32 total, i32 order, f32 periods[nfr], f32 lpc[nfr*(order+1)], f32 gains[nfr], f32 exc[total] */
    T_MARKS = 2,     /* i32 count, i32 frame[count], f32 period[count], i16 flag[count] */
    T_EXC = 3,       /* i32 epoch, i32 flag, i32 srcN, i32 dstN, f32 gain, f32 dst[dstN] */
    T_FILT = 4,      /* i32 n, f32 gain, i32 order, f32 a[order+1], f32 in[n], f32 out[n] */
    T_PCM = 5,       /* i32 n, i32 stereo, f32 samples[n] (before int16 conversion) */
    T_PFX = 6,       /* i32 n, f32 gain, f32 samples[n] (before post-fx) */
    T_SENT = 7,      /* raw dump of the sentence record from Speak_NextSentence (0x140 bytes) */
    T_PHONES = 8,    /* i32 count, then count 0x2c-byte phone records after unit selection */
    T_PROS_IN = 9,   /* f32 base_pitch, f32 range, i32 n, n * ITEM_DUMP bytes (items before prosody) */
    T_PROS_OUT = 10, /* i32 n, n * ITEM_DUMP bytes, i32 contour_len, f32 contour[len], 0x40 bytes of prosody object */
    T_LOOKUP = 13,   /* word struct after FUN_5ed4b1f9 (lexicon + morphology + LTS), LOOKUP_DUMP bytes */
    T_WORDS_PASS = 12, /* i32 pass (0 before 454df, 1 before 45651, 2 before 43f71, 3 after 43f71), i32 n, n * WORD_DUMP */
    T_TAG_IN = 14,   /* i32 n, n * 0x848 tagger entries before FUN_5ed4a436 */
    T_TAG_OUT = 15,  /* same, after */
    T_NODES = 16,    /* token list after FUN_5ed4b6e0, see h_Norm */
    T_WORD = 11,     /* word record given to the item builder: f32 ctx_rate(+0x67c of arg1 or -1), i32 prev_bound(+0x674 of arg3 or -1), WORD_DUMP bytes */
};

static FILE *g_log;
static BYTE *g_base;

static void rec(unsigned tag, const void *parts[], const unsigned lens[], int nparts)
{
    unsigned total = 0;
    int i;
    for (i = 0; i < nparts; i++) total += lens[i];
    fwrite(&tag, 4, 1, g_log);
    fwrite(&total, 4, 1, g_log);
    for (i = 0; i < nparts; i++) fwrite(parts[i], 1, lens[i], g_log);
}

/* ---- hot-patch hooks: replace the 5-byte "mov edi,edi; push ebp; mov ebp,esp" prologue ---- */
static void *hook(unsigned va, void *target)
{
    BYTE *fn = g_base + (va - IMAGE_BASE);
    BYTE *tramp = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    DWORD old;
    if (memcmp(fn, "\x8b\xff\x55\x8b\xec", 5) != 0) {
        fprintf(stderr, "unexpected prologue at %08x\n", va);
        ExitProcess(2);
    }
    memcpy(tramp, fn, 5);
    tramp[5] = 0xE9;
    *(DWORD *)(tramp + 6) = (DWORD)((fn + 5) - (tramp + 10));
    VirtualProtect(fn, 5, PAGE_EXECUTE_READWRITE, &old);
    fn[0] = 0xE9;
    *(DWORD *)(fn + 1) = (DWORD)((BYTE *)target - (fn + 5));
    VirtualProtect(fn, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 5);
    return tramp;
}

/* Voice_GetUnit: HRESULT __stdcall (void *voice, int unit, int out[7]) */
typedef HRESULT(__stdcall *GetUnit_t)(void *, int, int *);
static GetUnit_t o_GetUnit;
static HRESULT __stdcall h_GetUnit(void *self, int unit, int *out)
{
    HRESULT hr = o_GetUnit(self, unit, out);
    if (SUCCEEDED(hr)) {
        int nfr = out[0], total = out[1], order = out[2];
        int hdr[4];
        const void *p[5];
        unsigned l[5];
        hdr[0] = unit; hdr[1] = nfr; hdr[2] = total; hdr[3] = order;
        p[0] = hdr; l[0] = 16;
        p[1] = (void *)out[3]; l[1] = nfr * 4;
        p[2] = (void *)out[4]; l[2] = nfr * (order + 1) * 4;
        p[3] = (void *)out[6]; l[3] = nfr * 4;
        p[4] = (void *)out[5]; l[4] = total * 4;
        rec(T_UNIT, p, l, 5);
    }
    return hr;
}

/* PitchMarks_Place: __fastcall(this, edx, sentence, nframes, float rate, maxEpochs) -> count in EAX */
typedef __int64(__fastcall *Marks_t)(BYTE *, int, int, int, float, int);
static Marks_t o_Marks;
static __int64 __fastcall h_Marks(BYTE *self, int edx, int sent, int nfr, float rate, int maxe)
{
    __int64 r = o_Marks(self, edx, sent, nfr, rate, maxe);
    int count = (int)r;
    const void *p[4];
    unsigned l[4];
    p[0] = &count; l[0] = 4;
    p[1] = *(void **)(self + 0x38); l[1] = count * 4;
    p[2] = *(void **)(self + 0x3c); l[2] = count * 4;
    p[3] = *(void **)(self + 0x40); l[3] = count * 2;
    rec(T_MARKS, p, l, 4);
    return r;
}

/* Excitation_FetchPeriod: thiscall(this, float *src, uint srcN, float *dst, uint dstN, float gain) */
typedef void(__fastcall *Exc_t)(BYTE *, int, float *, unsigned, float *, unsigned, float);
static Exc_t o_Exc;
static void __fastcall h_Exc(BYTE *self, int edx, float *src, unsigned srcN, float *dst, unsigned dstN, float gain)
{
    int hdr[4];
    const void *p[3];
    unsigned l[3];
    o_Exc(self, edx, src, srcN, dst, dstN, gain);
    hdr[0] = *(int *)(self + 0x78);
    hdr[1] = (*(short **)(self + 0x40))[hdr[0]];
    hdr[2] = (int)srcN;
    hdr[3] = (int)dstN;
    p[0] = hdr; l[0] = 16;
    p[1] = &gain; l[1] = 4;
    p[2] = dst; l[2] = dstN * 4;
    rec(T_EXC, p, l, 3);
}

/* LPC_AllPoleFilter: thiscall(this, float *a, float *buf, int n, float gain); filters buf in place */
typedef void(__fastcall *Filt_t)(BYTE *, int, float *, float *, int, float);
static Filt_t o_Filt;
static float g_in[8192];
static void __fastcall h_Filt(BYTE *self, int edx, float *a, float *buf, int n, float gain)
{
    int order = *(int *)(self + 0x98);
    int hdr[1];
    const void *p[6];
    unsigned l[6];
    if (n > 8192) n = 8192;
    memcpy(g_in, buf, n * 4);
    o_Filt(self, edx, a, buf, n, gain);
    hdr[0] = n;
    p[0] = hdr; l[0] = 4;
    p[1] = &gain; l[1] = 4;
    p[2] = &order; l[2] = 4;
    p[3] = a; l[3] = (order + 1) * 4;
    p[4] = g_in; l[4] = n * 4;
    p[5] = buf; l[5] = n * 4;
    rec(T_FILT, p, l, 6);
}

/* FloatToPCM16: __stdcall(float *buf (converted in place to short), int n, int stereo, ?) */
typedef void(__stdcall *Pcm_t)(float *, int, int, int);
static Pcm_t o_Pcm;
static void __stdcall h_Pcm(float *buf, int n, int stereo, int x)
{
    const void *p[3];
    unsigned l[3];
    p[0] = &n; l[0] = 4;
    p[1] = &stereo; l[1] = 4;
    p[2] = buf; l[2] = n * 4;
    rec(T_PCM, p, l, 3);
    o_Pcm(buf, n, stereo, x);
}

/* Voice method +0x10 (FUN_5ed58bfa): __stdcall(voice, rec[n] (0x2c bytes each), n) - unit selection */
typedef HRESULT(__stdcall *Units_t)(void *, BYTE *, unsigned);
static Units_t o_Units;
static HRESULT __stdcall h_Units(void *self, BYTE *recs, unsigned n)
{
    HRESULT hr = o_Units(self, recs, n);
    const void *p[2];
    unsigned l[2];
    p[0] = &n; l[0] = 4;
    p[1] = recs; l[1] = n * 0x2c;
    rec(T_PHONES, p, l, 2);
    return hr;
}

/* Prosody (FUN_5ed541ad): thiscall(prosody_obj, list, float base_pitch, float range).
 * list: +8 head node; node: [0] next, [2] item. */
#define ITEM_DUMP 0x12c
#define WORD_DUMP 0x698
typedef void(__fastcall *Pros_t)(BYTE *, int, BYTE *, float, float);
static Pros_t o_Pros;
static int list_items(BYTE *list, BYTE **items, int max)
{
    int n = 0;
    void **node = *(void ***)(list + 8);
    while (node && n < max) {
        if (node[2]) items[n++] = (BYTE *)node[2];
        node = (void **)node[0];
    }
    return n;
}
static void dump_items(unsigned tag, BYTE **items, int n, const void *pre, unsigned prelen)
{
    int i;
    unsigned total = prelen + 4 + (unsigned)n * ITEM_DUMP;
    fwrite(&tag, 4, 1, g_log);
    fwrite(&total, 4, 1, g_log);
    fwrite(pre, 1, prelen, g_log);
    fwrite(&n, 4, 1, g_log);
    for (i = 0; i < n; i++) fwrite(items[i], 1, ITEM_DUMP, g_log);
}
static void __fastcall h_Pros(BYTE *self, int edx, BYTE *list, float base, float range)
{
    static BYTE *items[4096];
    int n = list_items(list, items, 4096), len = 0;
    float hdr[2];
    hdr[0] = base; hdr[1] = range;
    dump_items(T_PROS_IN, items, n, hdr, 8);
    o_Pros(self, edx, list, base, range);
    n = list_items(list, items, 4096);
    { int i; for (i = 0; i < n; i++) if (*(int *)(items[i] + 0xcc) + 8 > len) len = *(int *)(items[i] + 0xcc) + 8; }
    {
        unsigned tag = T_PROS_OUT, total = 4 + (unsigned)n * ITEM_DUMP + 4 + (unsigned)len * 4 + 0x40;
        float *contour = *(float **)(self + 0x10);
        int i;
        fwrite(&tag, 4, 1, g_log);
        fwrite(&total, 4, 1, g_log);
        fwrite(&n, 4, 1, g_log);
        for (i = 0; i < n; i++) fwrite(items[i], 1, ITEM_DUMP, g_log);
        fwrite(&len, 4, 1, g_log);
        if (contour && !IsBadReadPtr(contour, len * 4)) fwrite(contour, 4, len, g_log);
        else { float z = 0; for (i = 0; i < len; i++) fwrite(&z, 4, 1, g_log); }
        fwrite(self, 1, 0x40, g_log);
    }
}

/* Item builder FUN_5ed42301: thiscall(list_obj, ctx, word_record, prev_ctx, pos) -> byte */
typedef unsigned char(__fastcall *Word_t)(BYTE *, int, BYTE *, BYTE *, BYTE *, int);
static Word_t o_Word;
static unsigned char __fastcall h_Word(BYTE *self, int edx, BYTE *ctx, BYTE *word, BYTE *prev, int pos)
{
    float rate = ctx ? *(float *)(ctx + 0x67c) : -1.0f;
    int pb = prev ? *(int *)(prev + 0x674) : -1;
    const void *p[3];
    unsigned l[3];
    p[0] = &rate; l[0] = 4;
    p[1] = &pb; l[1] = 4;
    p[2] = word; l[2] = WORD_DUMP;
    rec(T_WORD, p, l, 3);
    return o_Word(self, edx, ctx, word, prev, pos);
}

/* word-level passes FUN_5ed454df / FUN_5ed45651 / FUN_5ed43f71: __fastcall(engine); words list at +0x38 */
typedef void(__fastcall *Pass_t)(BYTE *);
static Pass_t o_Pass0, o_Pass1, o_Pass2;
static void dump_words(BYTE *eng, int pass)
{
    static BYTE *w[4096];
    int n = 0, i, cnt = *(int *)(eng + 0x40);
    void **node = *(void ***)(eng + 0x38);
    while (node && n < cnt && n < 4096 && !IsBadReadPtr(node, 12)) {
        w[n++] = (BYTE *)node[2];
        node = (void **)node[0];
    }
    unsigned tag = T_WORDS_PASS, total = 8 + (unsigned)n * WORD_DUMP;
    fwrite(&tag, 4, 1, g_log);
    fwrite(&total, 4, 1, g_log);
    fwrite(&pass, 4, 1, g_log);
    fwrite(&n, 4, 1, g_log);
    for (i = 0; i < n; i++) fwrite(w[i], 1, WORD_DUMP, g_log);
}
static void __fastcall h_Pass0(BYTE *e) { dump_words(e, 0); o_Pass0(e); }
static void __fastcall h_Pass1(BYTE *e) { dump_words(e, 1); o_Pass1(e); }
static void __fastcall h_Pass2(BYTE *e) { dump_words(e, 2); o_Pass2(e); dump_words(e, 3); }

/* FUN_5ed4b1f9: thiscall(normalizer, short *word_struct) -> HRESULT */
#define LOOKUP_DUMP 0x850
typedef int(__fastcall *Look_t)(BYTE *, int, BYTE *);
static Look_t o_Look;
static int __fastcall h_Look(BYTE *self, int edx, BYTE *w)
{
    int hr = o_Look(self, edx, w);
    const void *p[2];
    unsigned l[2];
    p[0] = &hr; l[0] = 4;
    p[1] = w; l[1] = LOOKUP_DUMP;
    rec(T_LOOKUP, p, l, 2);
    return hr;
}


/* POS tagger: void __cdecl FUN_5ed4a436(entries, n) */
typedef void(__cdecl *Tag_t)(BYTE *, unsigned);
static Tag_t o_Tag;
static void __cdecl h_Tag(BYTE *e, unsigned n)
{
    const void *p[2];
    unsigned l[2];
    p[0] = &n; l[0] = 4;
    p[1] = e; l[1] = n * 0x848;
    rec(T_TAG_IN, p, l, 2);
    o_Tag(e, n);
    rec(T_TAG_OUT, p, l, 2);
}

static void put32(BYTE **q, unsigned v) { memcpy(*q, &v, 4); *q += 4; }
static void put_ws(BYTE **q, const WCHAR *s, unsigned n)
{
    if (!s) n = 0;
    put32(q, n);
    if (n) { memcpy(*q, s, n * 2); *q += n * 2; }
}

/* Tagging driver: int __thiscall FUN_5ed4b6e0(this, list **, heap) */
typedef int(__fastcall *Norm_t)(void *, int, int **, void *);
static Norm_t o_Norm;
static int __fastcall h_Norm(void *self, int edx, int **list, void *heap)
{
    int hr = o_Norm(self, edx, list, heap);
    static BYTE buf[1 << 20];
    BYTE *q = buf + 4;
    unsigned nn = 0;
    int *node;
    for (node = *list; node; node = (int *)node[0]) {
        int *d = node + 2, k;
        int *ty = (int *)d[6];
        nn++;
        put32(&q, ty ? ty[0] : 0xffffffff);
        put32(&q, d[4]);
        put32(&q, d[2]);
        put32(&q, d[5]);
        put_ws(&q, (WCHAR *)d[0], d[1]);
        for (k = 0; k < d[4]; k++) {
            int *w = (int *)(d[3] + k * 0x1c);
            int *ws = (int *)w[0];
            put32(&q, ws ? ws[0] : 0xffffffff);
            put32(&q, ws ? ws[8] : 0);
            put32(&q, ws ? ws[9] : 0);
            put32(&q, w[6]);
            put_ws(&q, (WCHAR *)w[1], w[2]);
            put_ws(&q, (WCHAR *)w[3], w[4]);
            put_ws(&q, (WCHAR *)w[5], w[5] ? (unsigned)wcslen((WCHAR *)w[5]) : 0);
        }
        if (q - buf > (1 << 20) - 0x10000) break;
    }
    memcpy(buf, &nn, 4);
    {
        const void *p[1];
        unsigned l[1];
        p[0] = buf; l[0] = (unsigned)(q - buf);
        rec(T_NODES, p, l, 1);
    }
    return hr;
}

/* Front-end "next segment" method: vtable[0] of the object at synth+0,
 * thiscall(obj, BYTE **seg_out, int *done). Patched through the vtable on first use. */
typedef int(__fastcall *GetSeg_t)(void *, int, BYTE **, int *);
static GetSeg_t o_GetSeg;
static int __fastcall h_GetSeg(void *obj, int edx, BYTE **seg, int *done)
{
    int hr = o_GetSeg(obj, edx, seg, done);
    if (hr >= 0 && *done == 0 && *seg) {
        const void *p[1];
        unsigned l[1];
        p[0] = *seg; l[0] = 0x140;
        rec(T_SENT, p, l, 1);
    }
    return hr;
}

/* Speak_NextSentence: __fastcall(this) -> HRESULT; used only to find and patch the segment source */
typedef int(__fastcall *Sent_t)(BYTE *);
static Sent_t o_Sent;
static int __fastcall h_Sent(BYTE *self)
{
    if (!o_GetSeg) {
        void **vtbl = **(void ****)self;
        DWORD old;
        o_GetSeg = (GetSeg_t)vtbl[0];
        VirtualProtect(vtbl, 4, PAGE_READWRITE, &old);
        vtbl[0] = (void *)h_GetSeg;
        VirtualProtect(vtbl, 4, old, &old);
    }
    return o_Sent(self);
}

static const GUID SPDFID_WaveFormatEx_ = {0xC31ADBAE, 0x527F, 0x4ff5, {0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C}};

int wmain(int argc, wchar_t **argv)
{
    ISpVoice *voice = NULL;
    ISpObjectToken *tok = NULL;
    ISpStream *stream = NULL;
    WAVEFORMATEX wf;
    HRESULT hr;

    if (argc < 4) {
        fwprintf(stderr, L"usage: samtap \"text\" out.wav out.log\n");
        return 1;
    }
    g_base = (BYTE *)LoadLibraryW(ENGINE_PATH);
    if (!g_base) { fprintf(stderr, "cannot load engine\n"); return 1; }
    g_log = _wfopen(argv[3], L"wb");
    if (!g_log) return 1;

    {
        /* optional 4th arg: bitmask of hooks to install (default all) */
        unsigned mask = argc > 4 ? wcstoul(argv[4], NULL, 0) : 0xffff;
        if (mask & 1) o_GetUnit = (GetUnit_t)hook(0x5ed5957f, h_GetUnit);
        if (mask & 2) o_Marks = (Marks_t)hook(0x5ed42bda, h_Marks);
        if (mask & 4) o_Exc = (Exc_t)hook(0x5ed42e40, h_Exc);
        if (mask & 8) o_Filt = (Filt_t)hook(0x5ed42dd7, h_Filt);
        if (mask & 16) o_Pcm = (Pcm_t)hook(0x5ed42a8f, h_Pcm);
        if (mask & 32) o_Sent = (Sent_t)hook(0x5ed42f7f, h_Sent);
        if (mask & 64) o_Units = (Units_t)hook(0x5ed58bfa, h_Units);
        if (mask & 128) o_Pros = (Pros_t)hook(0x5ed541ad, h_Pros);
        if (mask & 256) o_Word = (Word_t)hook(0x5ed42301, h_Word);
        if (mask & 1024) o_Look = (Look_t)hook(0x5ed4b1f9, h_Look);
        if (mask & 2048) {
            o_Tag = (Tag_t)hook(0x5ed4a436, h_Tag);
            o_Norm = (Norm_t)hook(0x5ed4b6e0, h_Norm);
        }
        if (mask & 512) {
            o_Pass0 = (Pass_t)hook(0x5ed454df, h_Pass0);
            o_Pass1 = (Pass_t)hook(0x5ed45651, h_Pass1);
            o_Pass2 = (Pass_t)hook(0x5ed43f71, h_Pass2);
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("hooks installed\n");
    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void **)&voice);
    if (FAILED(hr)) { fprintf(stderr, "SpVoice %08lx\n", hr); return 1; }
    hr = CoCreateInstance(&CLSID_SpObjectToken, NULL, CLSCTX_ALL, &IID_ISpObjectToken, (void **)&tok);
    if (SUCCEEDED(hr)) hr = ISpObjectToken_SetId(tok, NULL, SAM_TOKEN, FALSE);
    if (SUCCEEDED(hr)) hr = ISpVoice_SetVoice(voice, tok);
    if (FAILED(hr)) { fprintf(stderr, "voice %08lx\n", hr); return 1; }

    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = 22050;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2;
    wf.nAvgBytesPerSec = 44100;
    wf.cbSize = 0;
    hr = CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void **)&stream);
    if (SUCCEEDED(hr)) hr = ISpStream_BindToFile(stream, argv[2], SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx_, &wf, 0);
    if (SUCCEEDED(hr)) hr = ISpVoice_SetOutput(voice, (IUnknown *)stream, TRUE);
    if (FAILED(hr)) { fprintf(stderr, "output %08lx\n", hr); return 1; }

    if (argv[1][0] == L'@') { /* @file.txt: speak the (UTF-8) contents of a file */
        FILE *tf = _wfopen(argv[1] + 1, L"rb");
        static char buf[1 << 20];
        static WCHAR wbuf[1 << 20];
        size_t n = tf ? fread(buf, 1, sizeof buf - 1, tf) : 0;
        if (tf) fclose(tf);
        buf[n] = 0;
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 1 << 20);
        hr = ISpVoice_Speak(voice, wbuf, SPF_DEFAULT, NULL);
    } else {
        hr = ISpVoice_Speak(voice, argv[1], SPF_DEFAULT, NULL);
    }
    ISpStream_Close(stream);
    fclose(g_log);
    printf("speak hr=%08lx\n", hr);
    ISpStream_Release(stream);
    ISpObjectToken_Release(tok);
    ISpVoice_Release(voice);
    CoUninitialize();
    return FAILED(hr);
}
