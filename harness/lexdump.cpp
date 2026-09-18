// lexdump: query Microsoft Sam's lexicons through SAPI (32-bit, C++).
//
//   lexdump enum lex|lts          try ISpLexicon::GetWords and print every entry
//   lexdump look lex|lts words... print pronunciations of the given words
//   lexdump file lex|lts in.txt   look up every line of in.txt (UTF-8)
// Output lines (UTF-8): word <TAB> lextype <TAB> POS(hex) <TAB> SAPI phone ids
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <stdio.h>
#include <string.h>

static const wchar_t *VOICE = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\MSSam";

static void out(const wchar_t *s)
{
    char buf[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof buf, NULL, NULL);
    if (n > 0) fputs(buf, stdout);
}

static ISpLexicon *open_lex(bool lts)
{
    ISpObjectToken *voice = NULL, *tok = NULL;
    ISpDataKey *key = NULL;
    ISpObjectTokenInit *init = NULL;
    ISpLexicon *lex = NULL;
    wchar_t id[512];
    swprintf(id, 512, L"%ls\\%ls", VOICE, lts ? L"LTS" : L"Lex");
    HRESULT hr = CoCreateInstance(CLSID_SpObjectToken, NULL, CLSCTX_ALL, IID_ISpObjectToken, (void **)&voice);
    if (SUCCEEDED(hr)) hr = voice->SetId(NULL, VOICE, FALSE);
    if (SUCCEEDED(hr)) hr = voice->OpenKey(lts ? L"LTS" : L"Lex", &key);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_SpObjectToken, NULL, CLSCTX_ALL, IID_ISpObjectTokenInit, (void **)&init);
    if (SUCCEEDED(hr)) hr = init->InitFromDataKey(NULL, id, key);
    if (SUCCEEDED(hr)) hr = init->QueryInterface(IID_ISpObjectToken, (void **)&tok);
    if (SUCCEEDED(hr)) hr = tok->CreateInstance(NULL, CLSCTX_ALL, IID_ISpLexicon, (void **)&lex);
    if (FAILED(hr)) {
        fprintf(stderr, "open %s failed: %08lx\n", lts ? "lts" : "lex", hr);
        return NULL;
    }
    return lex;
}

static void print_word(const wchar_t *w, SPWORDPRONUNCIATION *p)
{
    for (; p; p = p->pNextWordPronunciation) {
        wchar_t line[2048];
        int n = swprintf(line, 2048, L"%ls\t%d\t%x\t", w, (int)p->eLexiconType, (unsigned)p->ePartOfSpeech);
        for (const SPPHONEID *ph = p->szPronunciation; *ph && n < 2000; ph++) n += swprintf(line + n, 2048 - n, L"%u ", (unsigned)*ph);
        swprintf(line + n, 2048 - n, L"\n");
        out(line);
    }
}

static void look(ISpLexicon *lex, const wchar_t *w)
{
    SPWORDPRONUNCIATIONLIST list = {0};
    HRESULT hr = lex->GetPronunciations(w, 0x409, (DWORD)(eLEXTYPE_APP | eLEXTYPE_VENDORLEXICON | eLEXTYPE_LETTERTOSOUND | eLEXTYPE_USER), &list);
    if (FAILED(hr)) {
        wchar_t line[600];
        swprintf(line, 600, L"%ls\t-\t-\t(hr %08lx)\n", w, hr);
        out(line);
    } else {
        print_word(w, list.pFirstWordPronunciation);
    }
    CoTaskMemFree(list.pvBuffer);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3) {
        fwprintf(stderr, L"usage: lexdump enum|look|file lex|lts ...\n");
        return 1;
    }
    CoInitialize(NULL);
    bool lts = !wcscmp(argv[2], L"lts");
    ISpLexicon *lex = open_lex(lts);
    if (!lex) return 1;
    if (!wcscmp(argv[1], L"enum")) {
        DWORD gen = 0, cookie = 0;
        long total = 0;
        HRESULT hr;
        do {
            SPWORDLIST wl = {0};
            hr = lex->GetWords((DWORD)(eLEXTYPE_APP | eLEXTYPE_VENDORLEXICON | eLEXTYPE_LETTERTOSOUND), &gen, &cookie, &wl);
            if (FAILED(hr)) {
                fprintf(stderr, "GetWords failed %08lx\n", hr);
                break;
            }
            for (SPWORD *w = wl.pFirstWord; w; w = w->pNextWord) {
                print_word(w->pszWord, w->pFirstWordPronunciation);
                total++;
            }
            CoTaskMemFree(wl.pvBuffer);
        } while (hr == S_FALSE);
        fprintf(stderr, "%ld words\n", total);
    } else if (!wcscmp(argv[1], L"look")) {
        for (int i = 3; i < argc; i++) look(lex, argv[i]);
    } else if (!wcscmp(argv[1], L"file")) {
        FILE *f = _wfopen(argv[3], L"rb");
        if (!f) return 1;
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
            if (!n) continue;
            wchar_t w[1024];
            MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 1024);
            look(lex, w);
        }
        fclose(f);
    }
    lex->Release();
    CoUninitialize();
    return 0;
}
