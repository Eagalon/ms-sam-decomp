# Microsoft Sam in portable C

A reconstruction of the SAPI 5 "Microsoft Sam" voice (`spttseng.dll`, Windows XP's "Microsoft TTS
engine") in plain C99. It reads the **original voice data files** at runtime and produces the same
audio as the real engine: identical length and every sample within ±1 LSB of the 16-bit output.
About 99.7% of samples match exactly; the rest differ by float rounding.

No Microsoft code is included. The C was written from a disassembly and decompilation of the engine
and checked function by function against the running DLL.

**The voice data is not included.** You need `Sam.spd`, `LTTS1033.LXA` and `r1033tts.LXA` from your own
Microsoft Sam install (Windows XP, or the SAPI 5.1 runtime; look in
`C:\Program Files\Common Files\Microsoft Shared\Speech\` and its `1033` subfolder).

## Status

| stage | source | verified against the real engine |
|---|---|---|
| waveform synthesis (LSF/LPC vocoder, pitch marks, excitation) | `src/sam.c` | 99.7% of samples bit-exact, the rest ±1 LSB |
| compressed lexicon `LTTS1033.LXA` | `src/sam_lex.c` | 28,980 / 28,980 words |
| letter-to-sound `r1033tts.LXA` | `src/sam_lex.c` | 5,000 / 5,000 words |
| morphology (plural / past / -ing ... stems) | `src/sam_morph.c` | 2,000 / 2,000 words |
| text normalizer (sentences, abbreviations, numbers, dates, times, currency, phone numbers, ...) | `src/sam_norm.c` | token lists match on all test texts |
| part-of-speech tagger + homographs (read, live, wind, "the" before vowels ...) | `src/sam_pos.c` | 100 / 100 homograph sentences |
| phrasing, accents, durations, F0, unit selection | `src/sam_front.c` | exact |
| **end to end, text → WAV** | `sam_say` | **21/21 corpus sentences, 104/104 number/date lines, 100/100 homograph sentences, 180/180 paragraphs of *Alice in Wonderland* (Gutenberg edition, including its license text)** |

Not done yet:
- SAPI XML markup (`<rate>`, `<pitch>`, `<emph>`, `<spell>` ...) and user lexicons. Plain text only.
- The SAPI 4 Sam (`msttsl`, a different engine) is not started.

## Building

Needs a C99 compiler. There are no platform dependencies in the library.

```
src\build.bat x64          (MSVC; finds Visual Studio / Build Tools itself; output in build\x64\)
src\build_clang.bat        (clang -std=c99 -pedantic, output in build\clang\)
make                       (Linux / macOS / MinGW, output in build/)
```

### Standalone exe (voice data compiled in)

Put the three data files in `data/voice/`, then:

```
python tools/embed_data.py            (writes src/sam_data.c, ~10 MB)
src\build.bat x64                     (also builds build\x64\sam_standalone.exe)
make standalone
```

The result needs no data files, but it contains Microsoft's data, so keep it to yourself.
`src/sam_data.c` is in `.gitignore`.

## Running

Point `--data` at the folder holding `Sam.spd`, `LTTS1033.LXA` and `r1033tts.LXA`.

```
build\x64\sam_say.exe --data data\voice "Hello, my name is Microsoft Sam." out.wav
build\x64\sam_say.exe --data data\voice @story.txt out.wav        (read a UTF-8 file)
```

Experiment knobs (not in the original engine):

```
--pitch 1.5          scale every pitch value
--speed 0.7          slower (<1) or faster (>1)
--gain 2             louder / clipped
--vibrato 3 6        the engine's hidden vibrato: depth, rate in Hz
--no-reverse         turn off time-reversal of repeated unvoiced frames
--reverse-units      play every sound unit backwards ("inside out")
--rate 48000         run the vocoder at another sample rate (real 48 kHz synthesis, not resampling)
```

### Singing

`--sing` treats the text as a score, one word per line: the word with syllables split by `-`, then a
note and a length in beats per syllable. `- 2` is a rest, `tempo 120` sets the beats per minute.

```
tempo 100
twin-kle   C4 1  C4 1
twin-kle   G4 1  G4 1
lit-tle    A4 1  A4 1
star       G4 2
```

```
build\x64\sam_say.exe --data data\voice --sing @demo\twinkle.txt twinkle.wav
```

Consonants keep their natural length and the vowel stretches to fill the note. A trailing r, l, m,
n or ng shares the note with the vowel. Long vowels hold their steadiest frame instead of looping
the unit, so they don't stutter. Singing options:

```
--sing-vibrato 30 5.5   vibrato depth in cents and rate in Hz (fades in on long notes)
--transpose -12         shift every note (semitones), e.g. an octave down
--no-smooth             turn off frame interpolation (sounds choppier, more like stock Sam)
--smooth                turn frame interpolation on when speaking too
```

The library API is in `src/sam.h`. Call `sam_tts_new` with the three data files and a PCM callback,
then `sam_tts_speak`. At a lower level, `sam_synth_segment` renders single acoustic units, and
`sam_unit_decode` exposes the LPC frames of any of the 3,365 units.

## How the engine works (short version)

1. **Normalizer**: text is split at whitespace into tokens. Punctuation is peeled off the ends.
   Periods are resolved as sentence end, abbreviation (177-entry table), or initials. Each token is
   classified: word, number, ordinal, decimal, fraction, year, decade, percent, degrees, currency,
   clock time, duration, date (numeric or with month names), phone number, state + ZIP, range,
   hyphenated, or "spell it out". Anything unrecognized is spelled piece by piece. Sentences end at
   `. ! ?` or after 50 tokens.
2. **Lookup**: vendor lexicon, then morphology, then letter-to-sound, giving pronunciations and parts
   of speech.
3. **Tagger**: 63 Brill-style contextual rules pick the part of speech, and with it the pronunciation of
   homographs. Special-word rules handle *a*, *the*, *read*, *US*, *Dr.*/*St.*, units, and so on.
4. **Word prosody**: emphasis for `!`, phrase breaks, accents. Accent strength comes from the C
   runtime's `rand()` (seed 1). Numbers, times, phone numbers and currency get their own accent
   patterns and short pauses.
5. **Units**: decision trees in `Sam.spd` choose one of 3,365 diphone-like units per phone. Durations
   come from per-unit statistics with phrase-final lengthening. The F0 contour is built from
   top/mid/bottom lines plus accents and boundary tones.
6. **Synthesis**: a pitch-synchronous LPC vocoder. Vector-quantized LSF frames give the filters, and
   irfft pulses or noise give the excitation.

## Verification tools

- `harness/samtap.exe`: loads the real `spttseng.dll` and hot-patches internal functions. It speaks
  text through SAPI and logs internal records (units, pitch marks, filters, word records, tagger input
  and output, token nodes). Build it with `harness/build.bat` (32-bit MSVC).
- `tools/compare_corpus.py`: the 21 reference sentences in `ref/corpus`, C versus real WAVs (render the
  references yourself with `tools/render_ref.ps1`; they are not included).
- `tools/bulk_compare.py file.txt`: renders every line with both engines and compares the audio.
- `tools/norm_compare.py file.txt`: compares token classification, words, pronunciations and parts of
  speech line by line.
- Python models of each stage are in `tools/` (`samvoc.py`, `prosody_model.py`, `word_prosody.py`,
  `lxa.py`, `lts.py`, `morph.py`, ...).
- `tools/pe_read.py` reads data tables straight from the DLL. (The Ghidra output used during the work
  is not included, since it is Microsoft's code.)

## Fun facts

- **"soy" and the roflcopter.** The unit for the *oy* glide in "soy" is made entirely of unvoiced
  frames. When the engine stretches it, it repeats frames and plays every other copy backwards; the
  repeating noise pattern turns into a buzz at about 93 Hz, the famous helicopter sound. This port
  does it too (`--no-reverse` changes the texture).
- **Accents are random.** Word accent strength uses the C runtime's `rand()` seeded with 1, so Sam says
  the same text the same way every time, but changing an earlier sentence changes later accents.
- **Unpronounceable words kill the sentence.** In the real engine an unknown symbol makes SAPI reject
  the whole Speak call, and the last ~5000 samples of audio buffered so far are lost. The port
  reproduces this.

## License

MIT for the code in this repo (see `LICENSE`). Microsoft Sam's voice data and the original engine
are Microsoft's and are not included. The small tables in `src/sam_abbrev_tab.h`,
`src/sam_pos_rules.h` and `src/sam_words_tab.h` (abbreviations, tagger rules, special words) were
transcribed from the engine because the program needs them to behave the same way.
