/* Internal glue between the libsam modules. */
#ifndef SAM_INTERNAL_H
#define SAM_INTERNAL_H

#include "sam.h"
#include "sam_lex.h"

struct sam_tts {
    sam_voice *voice;
    sam_lexicon *lex;
    sam_lts *lts;
    sam_synth *synth;
    uint32_t rand_state; /* MSVC rand(): the engine's accent prominences come from it */
    /* quote / parenthesis state of the word builder (engine this+0xe8/0xe9, pitch offset 0xec,
     * range 0xf0, rate 0x28); kept across sentences like the engine does */
    int in_quote, in_paren;
    float st_off, st_rng, st_rate;
    /* output held back until the engine would have flushed it (see sam_tts_speak) */
    sam_pcm_cb out_cb;
    void *out_user;
    int16_t *pend;
    size_t npend, pcap;
    float transpose; /* sam_params.transpose, for sam_tts_sing */
};

int sam_synth_chunk_pos(const sam_synth *s);
void sam_synth_chunk_reset(sam_synth *s);

/* Raw access to a section of the .spd voice file (0..4). */
const uint8_t *sam_voice_section(const sam_voice *v, int index, uint32_t *size);

#endif
