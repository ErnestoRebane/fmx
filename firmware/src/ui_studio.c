/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, in a teenage-engineering-like style: black, four colours (one per
 * track and per knob: blue, green, yellow, orange), white for what you touch, red for recording,
 * big numbers, lowercase labels, four dials at the bottom that show what KNOB 1..4 do.
 * Screens: TRACKS (the performance view), BREAK (ui_break.c), the
 * LAYERS (a function button held: what the 16 white keys and the knobs do now), HOLD (a hold to
 * confirm: clear, save), REC (armed / free take). Every band is drawn into the canvas only when its
 * signature changed. */
static void trk_short_name(uint32_t c, char *b);
/* the BREAK screen (ui_break.c, after the input) */
static int on_break_page(void);
static void break_screen_draw(void);
static void break_screen_input(uint32_t pressed, uint32_t home);
static void break_leave(void);
static int brk_layer_tap(uint32_t layer);
static void brk_layer_open(uint32_t layer);
static void brk_key(uint32_t k, uint32_t down);
static uint32_t brk_keys_lit(void);
/* the CHORDS screen (ui_chord.c) */
static int on_chord_page(void);
static void chord_screen_draw(void);
static void chord_screen_input(uint32_t pressed, uint32_t home);
static void chord_leave(void);
static int chd_layer_tap(uint32_t layer);
static void chd_key(uint32_t k, uint32_t down);
static uint32_t chd_keys_lit(void);
/* the CHOP screen (ui_chop.c) */
static int on_chop_page(void);
static void chop_screen_draw(void);
static void chop_screen_input(uint32_t pressed, uint32_t home);
static void chop_leave(void);
static void chp_key(uint32_t k, uint32_t down);
static int chp_layer_tap(uint32_t layer);
static int chop_oct_led(int up);
static void chop_mode_set(uint32_t m);
static void chop_open(void);
/* the BASS screen (ui_bass.c) */
static int on_bass_page(void);
static void bass_screen_draw(void);
static void bass_screen_input(uint32_t pressed, uint32_t home);
static int bss_layer_tap(uint32_t layer);
static void bss_key(uint32_t k, uint32_t down);
static void bss_open(void);

/* ---------------------------------------------------------------- style --- */
#define TE_G1 RGB(26, 26, 30)            /* tiles */
#define TE_G2 RGB(54, 54, 60)            /* empty steps, dial tracks */
#define TE_G3 RGB(118, 118, 126)         /* labels */
#define TE_G4 RGB(196, 196, 204)         /* secondary text */
#define TE_RED RGB(255, 44, 52)          /* recording, erasing */
static const uint16_t TE_COL[4] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 98, 26)};
static const uint16_t TE_MID[4] = {RGB(26, 82, 170), RGB(20, 136, 76), RGB(170, 132, 16), RGB(170, 66, 18)};
static const uint16_t TE_DIM[4] = {RGB(14, 40, 86), RGB(10, 66, 38), RGB(86, 66, 8), RGB(86, 32, 8)};
#define TE_DRUM TE_COL[3]

/* ---- FMX: the PS1 frame (the UI mockup, 2026-10): a dithered navy ground, windows with a light bevel, white text
 * with a hard shadow, the knobs as gauges. Shared by every screen through te_header / te_dials, and by the DECK */
#define PS_BG0 RGB(28, 38, 120)                 /* the ground: navy at the top of the screen .. */
#define PS_BG1 RGB(1, 2, 12)                    /* .. black at its bottom (psx3d.c p3_backdrop, dithered) */
#define PS_EDGE RGB(207, 210, 230)              /* a window's bevel */
#define PS_INK RGB(7, 10, 44)                   /* its inner line; the text's shadow */
#define PS_LINE RGB(42, 52, 134)                /* rules */
#define PS_LBL RGB(154, 164, 230)               /* labels */
#define PS_TXT RGB(238, 240, 255)               /* values */
#define PS_YEL RGB(255, 226, 122)               /* what waits, what is turned */
#define PS_GRN RGB(124, 240, 168)
#define PS_RED RGB(255, 74, 82)
#define PS_RTX RGB(255, 122, 122)                /* red as text on the navy ground (PS_RED there: 4:1, short) */
#define PS_DIM RGB(58, 66, 144)
/* the tracks' own colours: BASS blue, CHORDS teal, CHOP pink, BREAK orange */
static const uint16_t TRK_COL[NTRK] = {RGB(74, 140, 255), RGB(64, 220, 200), RGB(255, 120, 170), RGB(255, 138, 58)};

static uint16_t mix565_ps(uint16_t a, uint16_t b, int32_t k)      /* a -> b, k 0..256 */
{
    int32_t r = (a >> 11) + ((((b >> 11) - (a >> 11)) * k) >> 8);
    int32_t g = ((a >> 5) & 63) + (((((b >> 5) & 63) - ((a >> 5) & 63)) * k) >> 8);
    int32_t bl = (a & 31) + ((((b & 31) - (a & 31)) * k) >> 8);
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static void ps_ground(int32_t y0)                /* the canvas (begun) as the ground at screen row y0 */
{
    int32_t oy = cv_oy;
    cv_oy = -y0;
    p3_backdrop(PS_BG0, PS_BG1);
    cv_oy = oy;
}
static int32_t ps_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)   /* shadowed */
{
    cv_text(x + 1, y + 1, f, s, PS_INK);
    return cv_text(x, y, f, s, c);
}
/* a window: its fill a gradient top to bottom, a 1-pixel bevel (edge), a dark line inside */
static void ps_win(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t edge)
{
    int32_t i;
    for (i = 2; i < h - 2; i++) {
        int32_t k = i * 256 / h;                         /* (44, 60, 174) -> (18, 26, 100) -> (11, 15, 69) */
        uint16_t c = k < 140 ? RGB(44 - 26 * k / 140, 60 - 34 * k / 140, 174 - 74 * k / 140)
                             : RGB(18 - 7 * (k - 140) / 116, 26 - 11 * (k - 140) / 116, 100 - 31 * (k - 140) / 116);
        cv_rect(x + 2, y + i, w - 4, 1, c);
    }
    cv_rect(x, y, w, 1, edge);
    cv_rect(x, y + h - 1, w, 1, edge);
    cv_rect(x, y, 1, h, edge);
    cv_rect(x + w - 1, y, 1, h, edge);
    cv_rect(x + 1, y + 1, w - 2, 1, PS_INK);
    cv_rect(x + 1, y + h - 2, w - 2, 1, PS_INK);
    cv_rect(x + 1, y + 1, 1, h - 2, PS_INK);
    cv_rect(x + w - 2, y + 1, 1, h - 2, PS_INK);
}
static void ps_upper(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; s[i] && i + 1u < n; i++)
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    d[i] = 0;
}
/* a gauge: w x 8, the knob's colour in two tones up to ratio (0..1000; -1: none), the frame white when turned */
static void ps_gauge(int32_t x, int32_t y, int32_t w, int32_t ratio, uint16_t c, int hot)
{
    int32_t f = ratio < 0 ? 0 : clamp(ratio, 0, 1000) * (w - 4) / 1000;
    cv_rect(x, y, w, 8, hot ? C_WHITE : RGB(106, 116, 184));
    cv_rect(x + 1, y + 1, w - 2, 6, RGB(19, 24, 62));
    if (f) {
        cv_rect(x + 2, y + 2, f, 2, mix565_ps(c, C_WHITE, 110));
        cv_rect(x + 2, y + 4, f, 2, c);
    }
}

static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}
static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}

/* the dial strip: KNOB 1..4, label + value under each (a dial with no label: an empty column); a
 * message replaces it */
static const char *te_hint_txt;                         /* a hint line under the gauges (a screen sets it before te_dials) */
static const uint16_t *te_dial_col;                  /* te_dials, once: the gauges' colours (else the knobs', TE_COL) */
static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{   /* FMX: the knobs as PS1 gauges, a column each (the label, the gauge, the value), 56 rows from y0 */
    uint32_t k;
    const char *hint = te_hint_txt;
    const uint16_t *col = te_dial_col ? te_dial_col : TE_COL;
    int tight = hint && y0 > 178;                       /* (56 rows: the rows closer, so the hint fits under them) */
    te_hint_txt = 0;
    te_dial_col = 0;
    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");
    if (hint)
        sig = studio_hash(sig, hint);
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u + col[k], lab[k]);
        sig = studio_hash(sig, val[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 240u - (uint32_t)y0, C_BLACK);
    ps_ground(y0);
    cv_rect(0, 0, 240, 1, PS_LINE);
    if (ui.msg_t) {                                     /* a message: a window, yellow */
        int32_t w = text_w(&FONT_S, ui.msg) + 24;
        if (w > 232)
            w = 232;
        ps_win((240 - w) / 2, 10, w, 34, PS_YEL);
        ps_text((240 - text_w(&FONT_S, ui.msg)) / 2, 19, &FONT_S, ui.msg, PS_YEL);
        cv_blit(0, (uint32_t)y0);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t x = 4 + 59 * (int32_t)k, hot = ui.hot_t && ui.hot_col == k;
        char u[12];
        if (!lab[k][0])
            continue;
        ps_upper(u, lab[k], sizeof u);
        ps_text(x, tight ? 1 : 3, &FONT_S, u, hot ? C_WHITE : PS_LBL);
        ps_gauge(x, tight ? 18 : 21, 55, ratio[k], col[k], hot);
        ps_text(x, tight ? 27 : 33, &FONT_S, val[k], hot ? PS_YEL : PS_TXT);
    }
    if (hint)                                           /* what the buttons do here */
        ps_text(4, tight ? 41 : 49, &FONT_S, hint, PS_LBL);
    cv_blit(0, (uint32_t)y0);
}

static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG && rec_wait)
        rec_wait = 0;                                   /* (an arm does not follow into the song page) */
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; ui.home = 0; song.seq_mode = 0; ui.force = 1;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: the first note starts it */
}

/* the loop position of track t: "bar.beat" in its pattern (1.1 .. ) */
static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* the header of the live screens: BPM, transport, the loop position, the beat lights, REC */
static void te_header(const char *title, uint16_t tc, uint32_t *cache)
{   /* FMX: the PS1 frame's top: the tempo large, the transport, bar.beat, the four beats, the screen's name */
    char b[12], u[16];
    uint32_t beat = clk_beat, k, playing = song.playing;
    int32_t x;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + (song.rec != 0) * 1999u +
                               (playing ? beat * 131u + TSEL->seq_idx * 7919u : 0u) + (uint32_t)song.g[G_CLOCK] * 77u +
                               live_on * 5u + (ui.bpm_t != 0) * 104729u + song.solo * 37u, title);
    sig = sig * 31u + tc + q_mode * 7919u + (q_waits() ? qq.gen * 104729u + q_bar_at() : 0u);
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 40, C_BLACK);
    ps_ground(0);
    fmt_int(b, song.g[G_BPM]);
    x = ps_text(5, 4, &FONT_L, b, ui.bpm_t ? PS_YEL : PS_TXT);
    ps_text(x + 3, 20, &FONT_S, "bpm", PS_LBL);
    if (playing) {                                     /* the transport: a triangle, or a square stopped */
        int32_t i;
        for (i = 0; i < 9; i++)
            cv_rect(96, 3 + i, i < 5 ? i + 1 : 9 - i, 1, PS_GRN);
        loop_pos(TSEL, b);                             /* bar.beat in the selected track's loop */
        ps_text(108, 1, &FONT_S, b, PS_TXT);
    } else {
        cv_rect(96, 4, 8, 8, PS_LBL);
        ps_text(108, 1, &FONT_S, live_on ? "song" : "loop", PS_LBL);   /* (what PLAY plays: live_play, studio_play) */
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(96 + (int32_t)k * 9, 24, 7, 5, playing && beat % 4u == k ? (k ? C_WHITE : PS_RED) : PS_DIM);
    ps_upper(u, title, sizeof u);
    ps_text(236 - text_w(&FONT_S, u), 3, &FONT_S, u, tc == TE_RED || tc == PS_RED ? PS_RTX : tc);
    if (song.rec) {                                    /* recording: a red disc, "REC" (and the click) */
        te_disc(228, 29, 4, PS_RED);
        ps_text(220 - text_w(&FONT_S, song.g[G_CLOCK] ? "REC CLICK" : "REC"), 21, &FONT_S,
                song.g[G_CLOCK] ? "REC CLICK" : "REC", PS_RTX);
    } else if (q_waits() && song.playing) {          /* Q: something waits, and where it lands (q.c) */
        char w[12];
        str_cpy(w, "WAIT @", sizeof w);
        fmt_int(w + 6, (int32_t)q_bar_at());
        ps_text(236 - text_w(&FONT_S, w), 21, &FONT_S, w, PS_YEL);
    } else if (song.solo) {
        ps_text(236 - text_w(&FONT_S, "SOLO"), 21, &FONT_S, "SOLO", C_WHITE);
    } else if (q_mode) {
        ps_text(236 - text_w(&FONT_S, q_mode == QM_BAR ? "Q BAR" : "Q 4BAR"), 21, &FONT_S,
                q_mode == QM_BAR ? "Q BAR" : "Q 4BAR", PS_GRN);
    }
    cv_rect(0, 39, 240, 1, PS_LINE);
    cv_blit(0, 0);
}

/* --------------------------------------------------------------- TRACKS --- */
/* swing, MPC style: "62%" */
static void swing_str(char *b, int32_t v)            /* 0 straight .. 100 the most (params.c F_SWING) */
{
    fmt_int(b, clamp(v, 0, 100));
}

/* ------------------------------------------------------------- key lights --- */
/* the level order of the knobs: ghost < soft < norm < hard */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }
static uint16_t pad_lit[BRK_SLICES];                    /* the BREAK track's slices, key LEDs: frames left lit */
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */
static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits, n[NPART][4], k, w;
    fm1_irq_off();
    hits = bp.hits;
    bp.hits = 0;
    for (i = 0; i < NPART; i++)
        for (w = 0; w < 4u; w++) {
            n[i][w] = note_hits[i][w];
            note_hits[i][w] = 0;
        }
    fm1_irq_on();
    for (i = 0; i < BRK_SLICES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note
     * (a 1/16 at GATE 50 %: 60..80 ms) is seen even when it ends between two frames */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
    if (song.sel < NPART && (n[song.sel][0] | n[song.sel][1] | n[song.sel][2] | n[song.sel][3]))
        for (k = 0; k < 27u; k++) {
            uint32_t note = kb_map(TSEL, k);
            if (note < 128u && (n[song.sel][note >> 5] >> (note & 31u)) & 1u)
                key_lit[k] = 6;
        }
}

/* ---------------------------------------------------------------- REC --- */
/* a big seven-segment digit, TE style: w x h, segments t thick */
static void te_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = SEG[d % 10u];
    int32_t hh = h / 2;
    if (m & 0x01) cv_rect(x + t, y, w - 2 * t, t, c);                       /* a */
    if (m & 0x02) cv_rect(x + w - t, y + t, t, hh - t, c);                  /* b */
    if (m & 0x04) cv_rect(x + w - t, y + hh, t, hh - t, c);                 /* c */
    if (m & 0x08) cv_rect(x + t, y + h - t, w - 2 * t, t, c);               /* d */
    if (m & 0x10) cv_rect(x, y + hh, t, hh - t, c);                         /* e */
    if (m & 0x20) cv_rect(x, y + t, t, hh - t, c);                          /* f */
    if (m & 0x40) cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, c);          /* g */
}

static uint8_t rec_shown;
/* the four tracks, compact, from y0: the one that records is framed in red */
static void rec_rows(uint32_t rt, uint32_t take, uint32_t y0, uint32_t *cache)
{
    uint32_t sig = rt * 3u + rec_wait + take * 5u + y0, i, j;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
        int32_t y = (int32_t)i * 19;
        char b[16];
        if (armed) cv_rect(0, y, 240, 18, TE_RED), cv_rect(1, y + 1, 238, 16, C_BLACK);
        cv_rect(4, y + 3, 12, 12, armed ? TE_COL[i] : TE_DIM[i]);
        trk_short_name(i, b);
        b[10] = 0;
        cv_text(22, y + 1, &FONT_S, b, armed ? C_WHITE : TE_G3);
        for (j = 0; j < 16u; j++) {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
            if (z == a) z = a + 1u;
            for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
            cv_rect(106 + (int32_t)j * 8, y + 5, 6, 8, on ? (armed ? TE_COL[i] : TE_DIM[i]) : TE_G1);
        }
    }
    cv_blit(0, y0);
}

/* REC armed (stopped: waiting for the first note, or PLAY), its count-in, or a free take running
 * (seq.c). Armed, the dials say how it records: KNOB 1 MODE free / tempo (an empty project),
 * KNOB 2 LENGTH (1, 2 or 4 bars), KNOB 3 START note / count (ui_input.c rec_knobs). The four tracks
 * stay in view; the REC LED blinks while armed, is lit during the take. */
static void rec_screen_draw(void)
{
    static uint32_t head, body, rows, foot, dials;
    static uint8_t layout;
    uint32_t sig, take = ft_on, empty = take || project_empty(), rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    uint32_t lay = take ? 1u : 2u, free = empty && !rec_tempo && !take, count = ci_on;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown || lay != layout) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    rec_shown = 1;
    layout = (uint8_t)lay;
    te_header(take ? "free take" : count ? "count-in" : "rec ready", TE_RED, &head);
    if (take) {                                         /* free take: the time, the loop it makes */
        sig = 1000003u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
        if (ui.force || sig != body) {
            char b[24];
            uint32_t n = secs > 99u ? 99u : secs;
            body = sig;
            cv_begin(240, 106, C_BLACK);
            te_disc(18, 30, 9, blink ? TE_RED : TE_DIM[3]);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, TE_COL[rt & 3u]);
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "bar" : "bars", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "bpm", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "press rec on the 1", TE_RED);
            cv_blit(0, 42);
        }
        rec_rows(rt, take, 148u, &rows);
        if (ui.force || foot != 1u) {
            foot = 1u;
            cv_begin(240, 16, C_BLACK);
            cv_text(4, 0, &FONT_S, "rec: close", TE_G3);
            cv_text(236 - text_w(&FONT_S, "play: drop"), 0, &FONT_S, "play: drop", TE_G3);
            cv_blit(0, 224);
        }
        return;
    }
    foot = 0;
    /* armed / counting in: what happens next */
    sig = 2000003u + empty * 7u + free * 11u + rec_count * 13u + count * 17u + ci_beat * 19u + blink * 31u;
    if (ui.force || sig != body) {
        static const char *const L1[3] = {"play freely", "play a note", "press play"};
        static const char *const L2[3] = {"then rec on the 1", "it starts the loop", "4 clicks, then rec"};
        static const char *const L3[3] = {"the tempo follows you", "play: go  rec: cancel", "rec: cancel"};
        uint32_t m = free ? 0u : rec_count ? 2u : 1u;
        body = sig;
        cv_begin(240, 64, C_BLACK);
        if (count) {                                    /* the count-in: 4, 3, 2, 1 */
            te_digit(96, 4, 30, 54, 6, 4u - (ci_beat > 3u ? 3u : ci_beat), C_WHITE);
            te_disc(60, 31, 12, TE_RED);
            cv_text(144, 14, &FONT_S, "count-in", TE_G4);
            cv_text(144, 34, &FONT_S, "rec: cancel", TE_G3);
        } else {
            te_disc(28, 30, 18, blink ? TE_RED : TE_DIM[3]);
            te_disc(28, 30, 7, C_BLACK);
            cv_text(60, 6, &FONT_S, L1[m], C_WHITE);
            cv_text(60, 24, &FONT_S, L2[m], TE_G4);
            cv_text(60, 42, &FONT_S, L3[m], TE_G3);
        }
        cv_blit(0, 42);
    }
    rec_rows(rt, 0u, 106u, &rows);
    {   /* the dials: how it records */
        static char v[3][10];
        static const char *const LAB_E[4] = {"mode", "length", "start", ""};
        static const char *const LAB_F[4] = {"mode", "", "", ""};
        static const char *const LAB_N[4] = {"", "length", "start", ""};
        static const char *const LAB_0[4] = {"", "", "", ""};
        const char *val[4] = {v[0], v[1], v[2], ""};
        int32_t ratio[4] = {0, 0, 0, 0};
        uint32_t len = (uint32_t)clamp(TSEL->p[P_SLEN], 1, 64);
        str_cpy(v[0], rec_tempo ? "tempo" : "free", sizeof v[0]);
        if (len % 16u == 0u) {
            fmt_int(v[1], (int32_t)(len / 16u));
            str_cpy(v[1] + str_len(v[1]), len == 16u ? " bar" : " bars", 6);
        } else {
            fmt_int(v[1], (int32_t)len);
            str_cpy(v[1] + str_len(v[1]), " st", 4);
        }
        str_cpy(v[2], rec_count ? "count" : "note", sizeof v[2]);
        ratio[0] = rec_tempo ? 1000 : 0;
        ratio[1] = (int32_t)(len - 1u) * 1000 / 63;
        ratio[2] = rec_count ? 1000 : 0;
        {
            const char *const *lab = count ? LAB_0 : !empty ? LAB_N : free ? LAB_F : LAB_E;
            uint32_t k;
            for (k = 0; k < 4u; k++)
                if (!lab[k][0])
                    val[k] = "";                        /* (a dial not shown: no value either) */
            te_hint_txt = count ? 0 : "KNOBS: HOW IT RECORDS";
            te_dials(184, lab, val, ratio, rt * 7u + count * 3u, &dials);
        }
    }
}
