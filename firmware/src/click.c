/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* the metronome (GLO > GLOBAL > CLICK, and the REC count-in): a voice of its own, mixed after the tracks (fx.c
 * mix_block), whatever a mute, a solo, a filter or a slicer. A short sine tick: 1568 Hz on a bar's first beat,
 * 1047 Hz on the others, about 12 ms to fade by 1/e. It costs a few hundred cycles a block while it sounds,
 * nothing after */
static struct { uint32_t ph, inc; int32_t env, gain; } click;   /* env: Q30, 0 = silent */
static uint32_t click_n;                         /* clicks so far, and the last one's accent (the host tests count them) */
static uint8_t click_acc;
static void click_on(int accent)
{
    click.ph = 0;
    click.inc = accent ? (uint32_t)(((uint64_t)1568u << 32) / FS) : (uint32_t)(((uint64_t)1047u << 32) / FS);
    click.gain = accent ? 20000 : 12000;
    click.env = 1 << 30;
    click_acc = (uint8_t)(accent != 0);
    click_n++;
}
static void click_render(int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t i;
    int32_t g;
    if (!click.env)
        return;
    g = click.gain;
    for (i = 0; i < n; i++) {
        int32_t v = (int32_t)(((int64_t)sine_i(click.ph) * (click.env >> 15) >> 15) * g >> 15);
        ml[i] += v;
        mr[i] += v;
        click.ph += click.inc;
        click.env -= click.env >> 9;                 /* about 12 ms to 1/e */
    }
    if (click.env < (1 << 18))
        click.env = 0;                                /* (-72 dB: done, about 100 ms) */
}

