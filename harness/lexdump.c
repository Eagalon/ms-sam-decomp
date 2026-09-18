/* lexdump: query Microsoft Sam's lexicons through SAPI (32-bit).
 *
 *   lexdump enum lex|lts          try ISpLexicon::GetWords and print every entry
 *   lexdump look lex|lts words... print pronunciations of the given words
 *   lexdump file lex|lts in.txt   look up every line of in.txt
 * Output lines: word <TAB> lextype <TAB> POS(hex) <TAB> SAPI phone ids (space separated)
 */
#define _CRT_SECURE_NO_WARNINGS
#define COBJMACROS
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <stdio.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>

#define TOKEN_VOICE L"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech\Voices\Tokens\MSSam"
#define TOKEN_LEX L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\MSSam\\Lex"
#define TOKEN_LTS L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\MSSam\\LTS"

static const GUID CLSID_Compressed = {0x90903716, 0x2F42, 0x11D3, {0x9C, 0x26, 0x00, 0xC0, 0x4F, 0x8E, 0xF8, 0x7C}};
static const GUID CLSID_Lts = {0x685879BA, 0x3263, 0x11D3, {0x9C, 0x26, 0x00, 0xC0, 0x4F, 0x8E, 0xF8, 0x7C}};

static ISpLexicon *open_lex(int lts)
{
    ISpObjectToken *tok = NULL;
    ISpLexicon *lex = NULL;
    ISpObjectWithToken *owt = NULL;
    int step = 0;
    ISpObjectToken *voice = NULL;
    ISpDataKey *key = NULL;
    ISpObjectTokenInit *init = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_SpObjectToken, NULL, CLSCTX_ALL, &IID_ISpObjectToken, (void **)&voice);
    if (SUCCEEDED(hr)) hr = ISpObjectToken_SetId(voice, NULL, TOKEN_VOICE, FALSE);
    if (SUCCEEDED(hr)) hr = ISpObjectToken_OpenKey(voice, lts ? L"LTS" : L"Lex", &key);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(&CLSID_SpObjectToken, NULL, CLSCTX_ALL, &IID_ISpObjectTokenInit, (void **)&init);
    if (SUCCEEDED(hr)) {
        step = 1;
        hr = ISpObjectTokenInit_InitFromDataKey(init, NULL, lts ? TOKEN_LTS : TOKEN_LEX, key);
    }
    if (SUCCEEDED(hr)) hr = ISpObjectTokenInit_QueryInterface(init, &IID_ISpObjectToken, (void **)&tok);
    if (SUCCEEDED(hr)) {
        step = 2;
        hr = ISpObjectToken_CreateInstance(tok, NULL, CLSCTX_ALL, &IID_ISpLexicon, (void **)&lex);
    }
    if (FAILED(hr) && step == 2) {
        fprintf(stderr, "token CreateInstance failed %08lx, trying CoCreate + SetObjectToken\n", hr);
        hr = CoCreateInstance(lts ? &CLSID_Lts : &CLSID_Compressed, NULL, CLSCTX_ALL, &IID_ISpLexicon, (void **)&lex);
        if (SUCCEEDED(hr)) {
            step = 3;
            hr = ISpLexicon_QueryInterface(lex, &IID_ISpObjectWithToken, (void **)&owt);
        }
        if (SUCCEEDED(hr)) {
            step = 4;
            hr = ISpObjectWithToken_SetObjectToken(owt, tok);
        }
        if (owt) ISpObjectWithToken_Release(owt);
    }
    if (FAILED(hr)) {
        fprintf(stderr, "open %s failed at step %d: %08lx\n", lts ? "lts" : "lex", step, hr);
        return NULL;
    }
    return lex;
}

static void print_word(const WCHAR *w, SPWORDPRONUNCIATION *p)
{
    for (; p; p = p->pNextWordPronunciation) {
        const SPPHONEID *ph;
        wprintf(L"%ls\t%d\t%x\t", w, (int)p->eLexiconType, (unsigned)p->ePartOfSpeech);
        for (ph = p->szPronunciation; *ph; ph++) wprintf(L"%u ", (unsigned)*ph);
        wprintf(L"\n");
    }
}

static void look(ISpLexicon *lex, const WCHAR *w)
{
    SPWORDPRONUNCIATIONLIST list = {0};
    HRESULT hr = ISpLexicon_GetPronunciations(lex, w, 0x409, eLEXTYPE_APP | eLEXTYPE_VENDORLEXICON | eLEXTYPE_LETTERTOSOUND | eLEXTYPE_USER, &list);
    if (FAILED(hr)) {
        wprintf(L"%ls\t-\t-\t(hr %08lx)\n", w, hr);
    } else {
        print_word(w, list.pFirstWordPronunciation);
    }
    CoTaskMemFree(list.pvBuffer);
}

int wmain(int argc, wchar_t **argv)
{
    ISpLexicon *lex;
    int lts, i;
    if (argc < 3) {
        fwprintf(stderr, L"usage: lexdump enum|look|file lex|lts ...\n");
        return 1;
    }
    _setmode(_fileno(stdout), 0x20000); /* _O_U8TEXT */
    CoInitialize(NULL);
    lts = !wcscmp(argv[2], L"lts");
    lex = open_lex(lts);
    if (!lex) return 1;
    if (!wcscmp(argv[1], L"enum")) {
        DWORD gen = 0, cookie = 0;
        HRESULT hr;
        long total = 0;
        do {
            SPWORDLIST wl = {0};
            SPWORD *w;
            hr = ISpLexicon_GetWords(lex, eLEXTYPE_APP | eLEXTYPE_VENDORLEXICON | eLEXTYPE_LETTERTOSOUND, &gen, &cookie, &wl);
            if (FAILED(hr)) {
                fwprintf(stderr, L"GetWords failed %08lx\n", hr);
                break;
            }
            for (w = wl.pFirstWord; w; w = w->pNextWord) {
                print_word(w->pszWord, w->pFirstWordPronunciation);
                total++;
            }
            CoTaskMemFree(wl.pvBuffer);
        } while (hr == S_FALSE);
        fwprintf(stderr, L"%ld words\n", total);
    } else if (!wcscmp(argv[1], L"look")) {
        for (i = 3; i < argc; i++) look(lex, argv[i]);
    } else if (!wcscmp(argv[1], L"file")) {
        FILE *f = _wfopen(argv[3], L"r, ccs=UTF-8");
        WCHAR line[512];
        if (!f) return 1;
        while (fgetws(line, 512, f)) {
            WCHAR *e = line + wcslen(line);
            while (e > line && (e[-1] == L'\n' || e[-1] == L'\r')) *--e = 0;
            if (*line) look(lex, line);
        }
        fclose(f);
    }
    ISpLexicon_Release(lex);
    CoUninitialize();
    return 0;
}
