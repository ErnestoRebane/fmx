/* SPDX-License-Identifier: GPL-3.0-only */
/* The SONG screen (FMX): the songs. SAVE (tapped anywhere in STUDIO, or in LIVE) opens it, on the SET page. STUDIO makes
 * the song here, LIVE plays it: PLAY on this screen is STUDIO's (the loop, ui_input.c studio_play). Four pages, SELECT
 * turns them:
 *   SET    the four songs of the set as cards (the slot, its tempo and form; the one loaded marked): KNOB 1 the slot,
 *          2 LOAD, 3 SAVE (the loop you play into the slot), 4 NEW (an empty project); a GO knob is turned twice
 *          to do it. LIVE plays these songs (OCT, white key 8)
 *   CHAIN  AUTO SONG (arranger_scene.c), the loop you made played as a whole track: the chain of sections the form
 *          makes (a chip each: its role, its bars; the one playing lit, one asked for framed, those played dim; the
 *          selected section outlined, a dot on a chip whose card is not empty), the song's progress, the selected
 *          section (its name, its bar when it plays, the tracks in it). KNOB 1 FORM (SHORT 32, CLUB 64, LONG 128
 *          bars), 2 VARIANT (another song from the same loop), 3 MODE (SONG: to the end; ENDLESS), 4 START (the
 *          section LIVE's PLAY starts from, a section of the chain; a yellow bar on its chip: arranger.c arr_armed,
 *          as LIVE's white keys arm it)
 *   CARD   the section's card (project.c, arranger.h sec_card_t): KNOB 1 LENGTH (the form's, or 4 / 8 / 16 / 32
 *          bars), 2 LOOP (the project's, or A..D: the BREAK's loop in it), 3 STYLE (the BREAK's), 4 BASS (the BASS
 *          sound, a preset of its engine)
 *   TRACKS KNOB 1..4 BASS, CHORDS, CHOP, BREAK: as the section's role says, ON, OFF, or under water
 * ALGORITHM picks the section (INTRO .. OUTRO) the CHAIN, CARD and TRACKS pages show. A card field at its first value is
 * the project's: a card changes only what it says. The cards, FORM, VARIANT and MODE are the project's: SET keeps them.
 * A card is heard when its section starts (LENGTH: from the next PLAY, the chain is made then).
 * PLAY the transport; SAVE: the SET page; HOME goes back. (A section on the next bar: LIVE's white keys.) */
#define SG_COL RGB(255, 226, 122)                       /* yellow: the SONG screen */
static const uint16_t AR_COL[AR_N] = {RGB(90, 150, 255), RGB(64, 220, 200), RGB(255, 120, 170), RGB(255, 138, 58),
                                      RGB(150, 130, 255), RGB(255, 226, 122), RGB(120, 120, 140)};
static int on_song_page(void) { return !ui.home && cur_page()->scope == SC_SONG; }
static uint32_t as_who(uint32_t role, uint32_t k);       /* arranger_scene.c (the card's state of track k in a section) */
static uint32_t as_role_mute(uint32_t role);             /* ... the tracks out in it */
enum { SG_SET, SG_CHAIN, SG_CARD, SG_TRACKS };
static uint8_t sg_page, sg_role;                        /* the page; the section the cards edit */
static int8_t live_song;                                /* (ui_live.c: the slot of the song loaded, -1 none) */
static int project_song_info(uint32_t slot, int32_t *bpm, uint32_t *form, uint32_t *var);   /* (project.c) */
static int project_info(uint32_t slot, int32_t *bpm);   /* (project.c) */
static void project_new(void);                          /* (ui_input.c) */
static void studio_play(void);                          /* (ui_input.c) */
static const char *const SG_WHO[4] = {"auto", "on", "off", "under"};
static const char *const SG_LOOP[5] = {"project", "A", "B", "C", "D"};
/* START: the section LIVE's PLAY starts from (armed, else where the song was left); stepped over the chain's roles */
static uint32_t sg_start(void) { return (uint32_t)(arr_armed >= 0 ? arr_armed : (int8_t)arr_resume) % AR_N; }
static int sg_in_chain(uint32_t r)
{
    uint32_t i;
    for (i = 0; i < arrangement.count; i++)
        if (arrangement.entry[i].scene % AR_N == r)
            return 1;
    return 0;
}
static void sg_start_step(int32_t s)
{
    int32_t r = (int32_t)sg_start(), d = s > 0 ? 1 : -1, n;
    for (; s; s -= d)
        for (n = r + d; n >= 0 && n < (int32_t)AR_N; n += d)
            if (sg_in_chain((uint32_t)n)) {
                r = n;
                break;
            }
    arr_armed = (int8_t)r;
}
static int sg_card_empty(uint32_t r)
{
    const sec_card_t *c = &sec_card[r % AR_N];
    return !(c->len | c->who | c->loop | c->style | c->bass);
}
static void sg_fit(char *d, const char *s, uint32_t n)  /* s in lower case, as wide as a dial (55 px) at most */
{
    te_lower(d, s, n);
    while (d[0] && text_w(&FONT_S, d) > 54)
        d[str_len(d) - 1u] = 0;
}
static uint32_t sg_bass_n(void) { return ENGINES[trk[0].eng_req % NENGINES]->npresets; }

/* the SET page: the four songs, read from the slots (RAM, or flash once) when the page opens, after a LOAD / SAVE / NEW
 * and now and then (a slot's checksum is a read of 3.8 KB: not every frame) */
static struct { int32_t bpm; uint8_t used, form, var; } sg_slot[4];
static uint8_t sg_slot_t;                               /* frames to the next read */
static void sg_set_refresh(void)
{
    uint32_t i, f, v;
    for (i = 0; i < 4u; i++) {
        int32_t bpm = 0;
        sg_slot[i].used = (uint8_t)project_song_info(i, &bpm, &f, &v);
        sg_slot[i].bpm = bpm;
        sg_slot[i].form = (uint8_t)f;
        sg_slot[i].var = (uint8_t)v;
    }
    sg_slot_t = 90;
}
static const char *const SG_GO[4] = {"slot", "load", "save", "new"};
static uint32_t sg_form_bars(uint32_t form) { return form == 0u ? 32u : form == 1u ? 64u : 128u; }

static void sg_set_draw(void)
{
    static uint32_t cards_sig, info_sig;
    uint32_t i, sel = (uint32_t)clamp(song.g[G_SLOT], 1, 4) - 1u, sig;
    char b[32];
    if (!sg_slot_t || ui.force)
        sg_set_refresh();
    else
        sg_slot_t--;
    sig = sel * 31u + (uint32_t)(live_song + 2) * 977u;
    for (i = 0; i < 4u; i++)
        sig = sig * 31u + sg_slot[i].used * 7u + (uint32_t)sg_slot[i].bpm * 13u + sg_slot[i].form * 3u + sg_slot[i].var;
    if (sg_slot[sel].used)
        sig = sig * 7u + ui.frame / 3u;                 /* (the selected gem turns) */
    if (ui.force || sig != cards_sig) {                 /* the four cards, each a slot of the set */
        cards_sig = sig;
        cv_begin(240, 100, C_BLACK);
        for (i = 0; i < 4u; i++) {
            int32_t x = 6 + (int32_t)i * 58, y = 2;
            uint32_t on = i == sel, used = sg_slot[i].used;
            uint16_t edge = on ? C_WHITE : used ? PS_LBL : PS_DIM;
            cv_rect(x - (int32_t)on, y - (int32_t)on, 54 + 2 * (int32_t)on, 92 + 2 * (int32_t)on, edge);
            cv_rect(x + 1, y + 1, 52, 90, used ? RGB(8, 10, 44) : RGB(14, 18, 64));
            b[0] = (char)('1' + i);
            b[1] = 0;
            ps_text(x + 6, y + 3, &FONT_L, b, on ? C_WHITE : PS_LBL);
            if (used) {
                const uint16_t cols[2] = {TE_COL[i], RGB(150, 96, 255)};
                p3_mesh(P3_GEM_V, 6, P3_GEM_F, 8, cols, on ? ui.frame * 12u : 96u, 40u, x + 38, y + 22, 11, 80u, 0u);
                fmt_int(b, sg_slot[i].bpm);
                ps_text(x + 6, y + 44, &FONT_S, b, C_WHITE);
                ps_text(x + 6 + text_w(&FONT_S, b) + 3, y + 44, &FONT_S, "bpm", PS_LBL);
                if (sg_slot[i].form < 3u)
                    te_lower(b, N_AS_FORM[sg_slot[i].form], 8);
                else
                    str_cpy(b, "-", 2);
                ps_text(x + 6, y + 58, &FONT_S, b, SG_COL);
                if ((int32_t)i == live_song)
                    ps_text(x + 6, y + 73, &FONT_S, "loaded", PS_GRN);
            } else {
                ps_text(x + 27 - text_w(&FONT_S, "free") / 2, y + 40, &FONT_S, "free", PS_DIM);
            }
        }
        cv_blit(0, 42);
    }
    sig = sel * 31u + ui.arm * 977u + sg_slot[sel].used * 7u + (uint32_t)sg_slot[sel].form * 3u + (uint32_t)sg_slot[sel].bpm * 13u +
          (uint32_t)(live_song + 2) * 104729u;
    if (ui.force || sig != info_sig) {                  /* what the selected slot holds and what the knobs do to it */
        info_sig = sig;
        cv_begin(240, 32, C_BLACK);
        str_cpy(b, "slot ", 6);
        fmt_int(b + 5, (int32_t)sel + 1);
        ps_text(4, 0, &FONT_S, b, C_WHITE);
        if (sg_slot[sel].used) {
            str_cpy(b, "a song: ", 9);
            if (sg_slot[sel].form < 3u) {
                te_lower(b + 8, N_AS_FORM[sg_slot[sel].form], 8);
                str_cpy(b + str_len(b), " ", 2);
                fmt_int(b + str_len(b), (int32_t)sg_form_bars(sg_slot[sel].form));
                str_cpy(b + str_len(b), " bars", 6);
            } else {
                str_cpy(b + 8, "saved before the sections", 24);
            }
        } else {
            str_cpy(b, "empty", 6);
        }
        ps_text(4 + text_w(&FONT_S, "slot 0  "), 0, &FONT_S, b, SG_COL);
        if (ui.arm >= 0xD1u && ui.arm <= 0xD3u) {       /* a GO knob turned once */
            str_cpy(b, "again: ", 8);
            te_lower(b + 7, SG_GO[ui.arm - 0xD0u], 6);
            str_cpy(b + str_len(b), " ", 2);
            ps_text(4, 16, &FONT_S, b, PS_YEL);
        } else if (sg_slot[sel].used) {
            ps_text(4, 16, &FONT_S, "LOAD: replaces what you play", TE_G3);
        } else {
            ps_text(4, 16, &FONT_S, "SAVE: keeps what you play here", TE_G3);
        }
        cv_blit(0, 146);
    }
}

static void song_screen_draw(void)
{
    static uint32_t head, line_sig, body_sig, info_sig, foot;
    const arr_config_t *c = &arrangement;
    uint32_t run = arrangement_clock.running && song.playing, idx = run ? arrangement_clock.index : 0xFFu;
    uint32_t total = as_total(c), done = 0, i, sig, secs;
    int32_t jump = run ? arrangement_clock.jump : -1;
    char b[32];
    for (i = 0; run && i < idx && i < c->count; i++)
        done += c->entry[i].bars;
    if (run)
        done += arrangement_clock.bar;
    te_header(sg_page == SG_SET ? "song set" : sg_page == SG_CARD ? "song card" : sg_page == SG_TRACKS ? "song tracks" : "song chain",
              SG_COL, &head);
    if (sg_page == SG_SET) {
        sg_set_draw();
        goto dials;
    }
    secs = total * 240u / (uint32_t)(song.g[G_BPM] > 0 ? song.g[G_BPM] : 120);
    sig = total * 7u + secs * 31u + as_form() * 3u + as_endless() * 5003u;
    if (ui.force || sig != line_sig) {                  /* the form, its bars and time; the mode */
        line_sig = sig;
        cv_begin(240, 18, C_BLACK);
        te_lower(b, N_AS_FORM[as_form()], 8);
        str_cpy(b + str_len(b), "  ", 3);
        fmt_int(b + str_len(b), (int32_t)total);
        str_cpy(b + str_len(b), as_endless() ? "+ bars  " : " bars  ", 9);
        fmt_int(b + str_len(b), (int32_t)(secs / 60u));
        str_cpy(b + str_len(b), secs % 60u < 10u ? ":0" : ":", 3);
        fmt_int(b + str_len(b), (int32_t)(secs % 60u));
        cv_text(4, 0, &FONT_S, b, C_WHITE);
        str_cpy(b, as_endless() ? "endless" : "song", 8);   /* (the MODE: how LIVE plays it) */
        cv_text(236 - text_w(&FONT_S, b), 0, &FONT_S, b, SG_COL);
        cv_blit(0, 40);
    }
    sig = c->count * 7u + idx * 31u + (uint32_t)(jump + 1) * 977u + done * 13u + total * 3u + sg_role * 104729u +
          (run ? 0u : (sg_start() + 1u) * 7919u);
    for (i = 0; i < c->count; i++)
        sig = sig * 31u + c->entry[i].scene * 7u + c->entry[i].bars + (uint32_t)!sg_card_empty(c->entry[i].scene) * 15485863u;
    if (ui.force || sig != body_sig) {                  /* the chain: 6 a row; the song's progress */
        uint32_t st = run ? 0xFFu : sg_start(), st_shown = 0;
        body_sig = sig;
        cv_begin(240, 86, C_BLACK);
        for (i = 0; i < c->count && i < 12u; i++) {
            uint32_t r = c->entry[i].scene % AR_N, on = i == idx, past = run && i < idx;
            int32_t x = 6 + (int32_t)(i % 6u) * 38, y = (int32_t)(i / 6u) * 36;
            cv_rect(x, y, 36, 34, on ? AR_COL[r] : TE_G1);
            if (r == sg_role) {                         /* the section the cards edit: outlined */
                cv_rect(x, y, 36, 1, TE_G4), cv_rect(x, y + 33, 36, 1, TE_G4);
                cv_rect(x, y, 1, 34, TE_G4), cv_rect(x + 35, y, 1, 34, TE_G4);
            }
            if (!sg_card_empty(r))                      /* a card that says something: a dot */
                cv_rect(x + 28, y + 4, 5, 5, SG_COL);
            if (r == st && !st_shown) {                 /* START (stopped): where LIVE's PLAY begins, a yellow bar */
                cv_rect(x, y, 3, 34, SG_COL);
                st_shown = 1;
            }
            if ((int32_t)i == jump)
                cv_rect(x, y, 36, 2, C_WHITE), cv_rect(x, y + 32, 36, 2, C_WHITE);
            te_text_c(x + 18, y + 1, AR_SHORT[r], on ? C_BLACK : past ? TE_G3 : AR_COL[r]);
            fmt_int(b, c->entry[i].bars);
            te_text_c(x + 18, y + 17, b, on ? C_BLACK : TE_G3);
        }
        cv_rect(6, 76, 228, 6, TE_G1);
        if (total)
            cv_rect(6, 76, (int32_t)(228u * (done < total ? done : total) / total), 6, SG_COL);
        cv_blit(0, 58);
    }
    {   /* the selected section: its name, its bar if it plays; the tracks in it (the card's: on, under water, out) */
        uint32_t r = sg_role % AR_N, e = idx < c->count ? idx : 0u, here = run && c->entry[e].scene % AR_N == r;
        uint32_t m = as_role_mute(r), k;
        sig = r * 31u + (here ? (arrangement_clock.bar + 1u) * 977u : 0u) + (uint32_t)run * 5u + m * 7919u +
              (uint32_t)sec_card[r].who * 104729u + (uint32_t)sec_card[r].len * 1299709u;
        if (ui.force || sig != info_sig) {
            static const char *const TN[4] = {"bass", "chords", "chop", "break"};
            int32_t x;
            info_sig = sig;
            cv_begin(240, 32, C_BLACK);
            te_lower(b, AR_NAME[r], 12);
            cv_text(4, 0, &FONT_S, b, AR_COL[r]);
            if (here) {
                str_cpy(b, "bar ", 5);
                fmt_int(b + 4, (int32_t)arrangement_clock.bar + 1);
                str_cpy(b + str_len(b), " of ", 5);
                fmt_int(b + str_len(b), c->entry[e].bars);
            } else {
                str_cpy(b, sg_card_empty(r) ? "no card: as the song" : "card: when it starts", sizeof b);
            }
            cv_text(236 - text_w(&FONT_S, b), 0, &FONT_S, b, TE_G4);
            for (k = 0, x = 4; k < 4u; k++) {
                uint32_t in = !((m >> k) & 1u), under = as_who(r, k) == 3u;
                cv_rect(x, 18, 8, 8, in ? (under ? TE_G3 : AR_COL[r]) : TE_G1);
                if (under)
                    cv_rect(x + 1, 19, 6, 3, AR_COL[r]);   /* (half filled: under water) */
                cv_text(x + 12, 14, &FONT_S, TN[k], in ? TE_G4 : TE_G2);
                x += 12 + text_w(&FONT_S, TN[k]) + 10;
            }
            cv_blit(0, 146);
        }
    }
dials:
    {   /* KNOB 1..4, by page */
        static const char *const LAB0[4] = {"form", "variant", "mode", "start"};
        static const char *const LAB1[4] = {"length", "loop", "style", "bass"};
        static const char *const LAB2[4] = {"bass", "chords", "chop", "break"};
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        const char *const *lab = sg_page == SG_SET ? SG_GO : sg_page == SG_CARD ? LAB1 : sg_page == SG_TRACKS ? LAB2 : LAB0;
        const sec_card_t *cd = &sec_card[sg_role % AR_N];
        int32_t ratio[4];
        uint32_t k;
        if (sg_page == SG_SET) {
            fmt_int(v[0], clamp(song.g[G_SLOT], 1, 4));
            ratio[0] = (clamp(song.g[G_SLOT], 1, 4) - 1) * 333;
            for (k = 1; k < 4u; k++) {                  /* GO knobs: turn twice */
                str_cpy(v[k], ui.arm == 0xD0u + k ? "again" : "go", 12);
                ratio[k] = ui.arm == 0xD0u + k ? 1000 : 0;
            }
        } else if (sg_page == SG_CHAIN) {
            te_lower(v[0], N_AS_FORM[as_form()], 12);
            fmt_int(v[1], (int32_t)as_var());
            te_lower(v[2], N_AS_MODE[as_endless()], 12);
            sg_fit(v[3], AR_NAME[sg_start()], 12);
            ratio[0] = (int32_t)as_form() * 500;
            ratio[1] = (int32_t)as_var() * 1000 / 255;
            ratio[2] = (int32_t)as_endless() * 1000;
            ratio[3] = (int32_t)sg_start() * 1000 / (int32_t)(AR_N - 1u);
        } else if (sg_page == SG_CARD) {
            if (cd->len) {
                fmt_int(v[0], 4 << (cd->len - 1u));
                str_cpy(v[0] + str_len(v[0]), " bars", 6);
            } else {
                str_cpy(v[0], "form", 12);
            }
            str_cpy(v[1], SG_LOOP[cd->loop % 5u], 12);
            if (cd->style)
                sg_fit(v[2], N_BP_STYLE[(cd->style - 1u) % BS_NSTYLE], 12);
            else
                str_cpy(v[2], "project", 12);
            if (cd->bass && sg_bass_n())
                sg_fit(v[3], ENGINES[trk[0].eng_req % NENGINES]->presets[(cd->bass - 1u) % sg_bass_n()].name, 12);
            else
                str_cpy(v[3], "project", 12);
            ratio[0] = (int32_t)cd->len * 250;
            ratio[1] = (int32_t)cd->loop * 250;
            ratio[2] = (int32_t)cd->style * 1000 / (int32_t)BS_NSTYLE;
            ratio[3] = (int32_t)cd->bass * 1000 / (int32_t)(sg_bass_n() + 1u);
        } else {
            for (k = 0; k < 4u; k++) {
                str_cpy(v[k], SG_WHO[as_who(sg_role, k)], 12);
                ratio[k] = (int32_t)as_who(sg_role, k) * 333;
            }
        }
        te_hint_txt = sg_page == SG_SET ? "GO KNOBS: TURN TWICE" : sg_page == SG_CHAIN ? "ALGO: SECTION  LIVE PLAYS IT"
                                                                                    : "SELECT: PAGE  ALGO: SECTION";
        te_dials(178, lab, val, ratio, 211u + sg_page * 977u + sg_role * 31u + sg_start() * 104729u + (uint32_t)cd->len * 7u + (uint32_t)song.g[G_SLOT] * 13u + ui.arm * 5003u, &foot);
    }
}

static void song_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k;
    int32_t s;
    if (home == 1u) { go_home(); return; }       /* BT_TAP */
    for (k = 0; k < NB; k++) {
        if (!((pressed >> panel.btn[k]) & 1u)) continue;
        if ((k == B_PLAY || k == B_REC) && ft_owns_press())
            continue;                            /* (they close a free take: seq.c) */
        if (k == B_PLAY)
            studio_play();                       /* (ui_input.c: STUDIO plays the loop) */
        else if (k == B_SEQ)
            open_family(FAM_SEQ);
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {              /* the page: SET, CHAIN, CARD, TRACKS */
        sg_page = (uint8_t)clamp((int32_t)sg_page + s, 0, 3);
        sg_slot_t = 0;
        ui.force = 1;
    }
    if ((s = panel_enc(EN_ALGO)) != 0) {                /* the section the cards edit */
        sg_role = (uint8_t)clamp((int32_t)sg_role + s, 0, (int32_t)AR_N - 1);
        ui.force = 1;
    }
    for (k = 0; k < 4u; k++) {
        sec_card_t *cd = &sec_card[sg_role % AR_N];
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (sg_page == SG_SET) {                        /* the set: the slot; LOAD, SAVE, NEW (turned twice) */
            if (k == 0u) {
                song.g[G_SLOT] = (int16_t)clamp(song.g[G_SLOT] + s, 1, 4);
                ui.arm = 0;
            } else if (s < 0) {
                ui.arm = 0;
            } else if (ui.arm != 0xD0u + k && !(k == 2u && !project_info((uint32_t)song.g[G_SLOT] - 1u, 0))) {
                ui.arm = (uint8_t)(0xD0u + k);          /* (SAVE into an empty slot loses nothing: at once) */
                ui.arm_t = 90;
            } else {
                ui.arm = 0;
                ui.arm_t = 0;
                if (k == 1u) {
                    project_load((uint32_t)song.g[G_SLOT] - 1u);
                } else if (k == 2u) {
                    project_save((uint32_t)song.g[G_SLOT] - 1u);
                } else if (song.playing && arrangement_enabled) {
                    ui_message("STOP THE SONG FIRST");   /* (the song's stop would bring the old loop back) */
                } else {
                    project_new();
                    ui_message("NEW PROJECT");
                }
                sg_slot_t = 0;
            }
            ui.force = 1;
            continue;
        }
        if (sg_page == SG_CARD) {                       /* the card: LENGTH, LOOP, STYLE, BASS (0 = the project's) */
            uint32_t was = cd->len;
            if (k == 0u)
                cd->len = (uint8_t)clamp((int32_t)cd->len + s, 0, 4);
            else if (k == 1u)
                cd->loop = (uint8_t)clamp((int32_t)cd->loop + s, 0, 4);
            else if (k == 2u)
                cd->style = (uint8_t)clamp((int32_t)cd->style + s, 0, (int32_t)BS_NSTYLE);
            else
                cd->bass = (uint8_t)clamp((int32_t)cd->bass + s, 0, (int32_t)sg_bass_n());
            if (cd->len != was) {
                as_set(as_form(), as_var(), as_endless());   /* (the chain again, the length in it: the next PLAY's) */
                if (song.playing && arrangement_enabled)
                    ui_message("LENGTH: FROM NEXT PLAY");
            }
        } else if (sg_page == SG_TRACKS) {              /* TRACKS: the track's state in the section */
            int32_t w = clamp((int32_t)as_who(sg_role, k) + s, 0, 3);
            cd->who = (uint8_t)((cd->who & ~(3u << (2u * k))) | (uint32_t)w << (2u * k));
        } else if (k == 0u) {
            as_set((uint32_t)clamp((int32_t)as_form() + s, 0, 2), as_var(), as_endless());
        } else if (k == 1u) {
            as_set(as_form(), (uint32_t)(((int32_t)as_var() + 256 + s) % 256), as_endless());
        } else if (k == 2u) {
            as_set(as_form(), as_var(), s > 0);
        } else {
            sg_start_step(s);                           /* START: LIVE's PLAY from there (as its white keys arm it) */
        }
        settings_later = 1;
        ui.force = 1;
    }
    panel_enc(EN_PRESET);                               /* (no hidden action here) */
}
