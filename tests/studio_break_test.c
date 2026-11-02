/* SPDX-License-Identifier: GPL-3.0-only */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static uint32_t into_s(const track_t *t)          /* samples into the step the clock is in */
{
    uint32_t into, len;
    trk_grid(t, &into, &len);
    return into / (uint32_t)song.g[G_BPM];
}
/* FMX's studio on the BREAK track (the drum track SLOOP had): the slice maps of the keys and MIDI notes, the
 * built-in loops hit while stopped, the metronome, the record start and the free take, hits recorded into the steps */
int main(int argc,char **argv)
{
    uint32_t i,j,k,v,total=0; uint64_t energy[BRK_NBUILTIN]={0};
    int32_t out[CTL*2];
    host_tracks_init();
    /* the BREAK track's keys and notes: the white keys are the slices 1..8 in order (twice), a black key plays the
     * white key left of it; MIDI note 36 + k is slice k; a level from a velocity and back */
    for(i=0;i<27u;i++)assert(brk_slice_of_key(i)<BRK_SLICES);
    assert(brk_slice_of_key(0)==0 && brk_slice_of_key(1)==0 && brk_slice_of_key(2)==1 && brk_slice_of_key(11)==6 &&
           brk_slice_of_key(12)==7 && brk_slice_of_key(14)==0 && brk_slice_of_key(26)==7);
    for(i=0;i<BRK_SLICES;i++)assert(brk_slice_of_key(brk_key_of_slice(i))==i && brk_slice_of_note(36u+i)==i);
    assert(vel_lvl(30)==LV_GHOST && vel_lvl(70)==LV_SOFT && vel_lvl(100)==LV_NORM && vel_lvl(127)==LV_HARD);
    for(i=0;i<4u;i++)assert(vel_lvl(lvl_vel(i,100))==i);
    /* one hit of slice 3 from each built-in loop, stopped: each distinct, bounded, and silent once its slice has
     * played (a stopped hit ends with its slice) */
    for(i=0;i<BRK_NBUILTIN;i++) {
        host_tracks_init();
        TBRK->p[P_LRATE]=(int16_t)i;brkp_poll();
        assert(bpb[0].src && bpb[0].key==i);
        brk_hit(2,LV_HARD);
        for(j=0;j<FS*2u/CTL;j++) {
            mix_block(out,CTL);
            for(k=0;k<2u*CTL;k++){assert(out[k]>-32768 && out[k]<32768);if(j<FS/4u/CTL)energy[i]+=out[k]<0?-out[k]:out[k];}
        }
        assert(energy[i]>10000 && !bp.v[0].on && !bp.v[1].on && !bp.shot && bp.gate==0);
        for(k=0;k<i;k++)assert(energy[k]!=energy[i]);
    }
    /* LIVE metronome (seq.c click_tick): 120 BPM, 2 s = 4 beats; REC mode clicks only while a
     * track records, ON always while playing, OFF never; the first beat of the bar is louder */
    for(v=0;v<4;v++) {
        uint32_t hits=0,loud=0,age0;
        host_tracks_init();
        song.g[G_BPM]=120;song.g[G_CLOCK]=v==3?0:v==2?2:1;song.rec=v==0?1u:0u;rec_wait=0;   /* OFF / ON / REC */
        transport_req=1;age0=click_n;
        for(j=0;j<(FS*2u-FS/4u)/CTL;j++) {               /* up to just before beat 4 */
            uint32_t c=click_n;                          /* (the click is its own voice, click.c click_on) */
            mix_block(out,CTL);
            if(click_n!=c){hits++;loud+=click_acc;}
        }
        transport_req=2;events_block(CTL);
        if(v==0||v==2)assert(hits==4 && loud==1);
        else assert(hits==0 && click_n==age0 && !click.env);
    }
    /* LIVE record start. Stopped and armed, a project with notes: the first note starts the
     * loop and is step 1 */
    {
        uint32_t period, b, k0 = 7u, half, c;            /* key 7 = C4 */
        host_tracks_init();song.g[G_BPM]=120;song.g[G_CLOCK]=0;song.sel=0;song.playing=0;
        for(c=0;c<NTRK;c++)steps_clear(&trk[c]);
        TBRK->bstep[3].on=1;                             /* (not empty: no free take) */
        rec_wait=1;
        for(b=0;b<10;b++)mix_block(out,CTL);            /* nothing happens while it waits */
        assert(!song.playing && !song.rec && rec_wait==1 && !ft_on);
        fm1_in.notes=1u<<k0;mix_block(out,CTL);
        assert(song.playing && song.rec==1u && !rec_wait && rec_go && !ft_on);
        assert(trk[0].step[0].n==1 && trk[0].step[0].note[0]==60 && trk[0].step[0].time==ST_NOTE);
        fm1_in.notes=0;mix_block(out,CTL);
        transport_req=2;mix_block(out,CTL);
        assert(!song.playing && !song.rec && !rec_wait);     /* STOP ends the take */
        /* playing: recording starts at once; a note goes to the step heard: up to REC_LAT after
         * the middle of a step it is still that step, later the next one */
        song.sel=1;trk[1].p[P_SLEN]=16;period=div_samples((uint32_t)trk[1].p[P_SDIV]);half=period/2u;
        transport_req=1;mix_block(out,CTL);
        while(trk[1].seq_idx!=5u)mix_block(out,CTL);
        rec_begin();assert(song.rec==2u);
        while(into_s(&trk[1])<half+REC_LAT/2u)mix_block(out,CTL);
        fm1_in.notes=1u<<k0;mix_block(out,CTL);fm1_in.notes=0;mix_block(out,CTL);
        assert(trk[1].step[5].n==1 && !trk[1].step[6].n);   /* late by less than the latency: on 5 */
        while(trk[1].seq_idx!=9u)mix_block(out,CTL);
        while(into_s(&trk[1])<half+REC_LAT+2u*CTL)mix_block(out,CTL);
        fm1_in.notes=1u<<(k0+2u);mix_block(out,CTL);fm1_in.notes=0;mix_block(out,CTL);
        assert(!trk[1].step[9].n && trk[1].step[10].n==1 && trk[1].step[10].note[0]==62);
        transport_req=2;mix_block(out,CTL);song.rec=0;rec_wait=0;
    }
    /* FREE TAKE (an empty project): play with no tempo, REC on the downbeat closes the loop,
     * whose length sets the tempo; the notes and their lengths are quantised into it */
    {
        static const struct { double bpm; uint32_t bars; int16_t want_bpm, want_bars; } T[] = {
            {100.0, 1, 100, 1},     /* 1 bar at 100: 100 (200 is further from 120) */
            {118.0, 1, 120, 1},     /* within 3 % of the tempo set: kept */
            {90.0, 2, 90, 2},       /* 2 bars at 90: 45 / 90 / 180, 90 is the nearest 120 */
            {140.0, 4, 140, 4},     /* 4 bars at 140 (35 / 70 / 140) */
        };
        uint32_t ti, c, rb = 1u << 3, k0 = 7u;
        for(ti=0;ti<sizeof T/sizeof T[0];ti++){
            double beat=60.0/T[ti].bpm;
            uint32_t nb=T[ti].bars*4u, blk=0, bi, len=16u*(uint32_t)T[ti].want_bars;
            host_tracks_init();song.g[G_BPM]=120;song.g[G_CLOCK]=0;song.sel=0;song.playing=0;song.rec=0;
            for(c=0;c<NTRK;c++)steps_clear(&trk[c]);
            trk[0].step[40].time=ST_TIE;                 /* (an old tie: no effect) */
            ft_btn_mask=rb;fm1_in.buttons=0;ft_bars=0;
            rec_wait=1;mix_block(out,CTL);
            for(bi=0;bi<=nb;bi++){                       /* a note on every beat, the 2nd held 1/2 beat */
                uint32_t on=(uint32_t)(bi*beat*FS/CTL+0.5)+(bi&1u?6u:0u);   /* (+4 ms late on odd beats) */
                uint32_t off=on+(uint32_t)((bi==1u?beat/2.0:beat/8.0)*FS/CTL);
                if(bi==nb){                              /* the downbeat: REC closes */
                    while(blk<on){mix_block(out,CTL);blk++;}
                    fm1_in.buttons=rb;mix_block(out,CTL);blk++;fm1_in.buttons=0;
                    break;
                }
                while(blk<on){mix_block(out,CTL);blk++;}
                {static const uint8_t KOFF[4]={0,2,4,5};fm1_in.notes=1u<<(k0+KOFF[bi&3u]);}mix_block(out,CTL);blk++;
                if(bi==0)assert(ft_on && !song.playing && !rec_wait);
                while(blk<off){mix_block(out,CTL);blk++;}
                fm1_in.notes=0;mix_block(out,CTL);blk++;
            }
            assert(!ft_on && song.playing && !song.rec);
            assert(ft_bars==(uint8_t)T[ti].want_bars && song.g[G_BPM]==T[ti].want_bpm);
            for(c=0;c<NTRK;c++)assert(trk[c].p[P_SLEN]==(int16_t)len && trk[c].p[P_SDIV]==2);
            for(j=0;j<len;j++){
                const step_t *st=&trk[0].step[j];
                if(j%4u==0){
                    static const uint8_t KN[4]={60,62,64,65};
                    assert(st->time==ST_NOTE && st->n==1 && st->note[0]==KN[(j/4u)&3u]);
                } else if(j==5u) {
                    assert(st->time==ST_TIE);            /* the held note: 2 steps */
                } else {
                    assert(!st->n && st->time!=ST_NOTE && st->time!=ST_TIE);
                }
            }
            for(j=len;j<NSTEP;j++)assert(trk[0].step[j].time==ST_REST);
            transport_req=2;mix_block(out,CTL);
        }
        /* STOP during a free take drops it (so does the PLAY button); a PLAY from elsewhere (the editor)
         * closes it like REC */
        host_tracks_init();song.g[G_BPM]=120;song.playing=0;song.rec=0;song.sel=0;
        for(c=0;c<NTRK;c++)steps_clear(&trk[c]);
        ft_bars=0;rec_wait=1;fm1_in.notes=1u<<k0;mix_block(out,CTL);fm1_in.notes=0;
        assert(ft_on);
        for(j=0;j<100;j++)mix_block(out,CTL);
        transport_req=2;mix_block(out,CTL);
        assert(!ft_on && !song.playing && ft_bars==0xFF && !trk[0].step[0].n);
        ft_bars=0;rec_wait=1;fm1_in.notes=1u<<k0;mix_block(out,CTL);fm1_in.notes=0;
        for(j=0;j<(uint32_t)(2.0*FS/CTL);j++)mix_block(out,CTL);   /* 2 s: 1 bar at 120 */
        transport_req=1;mix_block(out,CTL);
        assert(!ft_on && song.playing && ft_bars==1 && song.g[G_BPM]==120 && trk[0].step[0].n==1);
        transport_req=2;mix_block(out,CTL);              /* the PLAY button during a take: dropped */
        for(c=0;c<NTRK;c++)steps_clear(&trk[c]);
        ft_drop_mask=1u<<5;ft_bars=0;rec_wait=1;fm1_in.notes=1u<<k0;mix_block(out,CTL);fm1_in.notes=0;
        for(j=0;j<200;j++)mix_block(out,CTL);
        fm1_in.buttons=ft_drop_mask;mix_block(out,CTL);fm1_in.buttons=0;mix_block(out,CTL);
        assert(!ft_on && !song.playing && ft_bars==0xFF && !trk[0].step[0].n);
        ft_drop_mask=0;
        /* the longest take closes by itself (24 s) */
        transport_req=2;mix_block(out,CTL);
        for(c=0;c<NTRK;c++)steps_clear(&trk[c]);
        ft_bars=0;rec_wait=1;fm1_in.notes=1u<<k0;mix_block(out,CTL);fm1_in.notes=0;
        while(ft_on)mix_block(out,CTL);
        assert(ft_bars==4 && song.g[G_BPM]==40 && song.playing);
        transport_req=2;mix_block(out,CTL);ft_btn_mask=0;
    }
    /* the BREAK track selected and armed, playing: a key records its slice into the step heard (OCT- held: ghost,
     * OCT+ held: hard); MIDI ch 10 and ch 4 the same, the level from the velocity */
    {
        static const uint8_t AT[4]={2,4,6,9},SL[4]={2,4,5,7},LV[4]={LV_GHOST,LV_HARD,LV_HARD,LV_SOFT};
        uint32_t h0;
        host_tracks_init();song.g[G_BPM]=120;song.g[G_CLOCK]=0;song.sel=3;song.rec=8u;
        for(j=0;j<NSTEP;j++)TBRK->bstep[j].on=0;
        dyn_bit[0]=1u<<3;dyn_bit[1]=1u<<4;                 /* (OCT- / OCT+: the panel's, ui_layers.c) */
        transport_req=1;mix_block(out,CTL);h0=bp.st_hits;
        for(i=0;i<4u;i++){
            while(TBRK->seq_idx!=AT[i])mix_block(out,CTL);
            if(i<2u){                                    /* the keys */
                fm1_in.buttons=dyn_bit[i];fm1_in.notes=1u<<brk_key_of_slice(SL[i]);mix_block(out,CTL);
                fm1_in.notes=0;fm1_in.buttons=0;mix_block(out,CTL);
            }else{                                       /* MIDI ch 10, ch 4 */
                midi_in_q[mi_w++%MQ]=0x09u|(i==2u?0x99u:0x93u)<<8|(36u+SL[i])<<16|(i==2u?120u:70u)<<24;
                mix_block(out,CTL);
            }
        }
        for(i=0;i<4u;i++){
            const bstep_t *b=&TBRK->bstep[AT[i]];
            assert(b->on && b->slice==SL[i]+1u && b->lvl==LV[i]);
        }
        assert(bp.st_hits==h0+4u && !TBRK->bstep[3].on && !TBRK->bstep[5].on);
        transport_req=2;mix_block(out,CTL);song.rec=0;dyn_bit[0]=dyn_bit[1]=0;
    }
    /* the arm follows the selected track (rec_follow), and a disarmed song stays disarmed */
    song.rec=1u;rec_follow(2);assert(song.rec==4u);rec_follow(3);assert(song.rec==8u);
    song.rec=0;rec_follow(1);assert(song.rec==0);
    total=(FS*4u/CTL)*CTL;
    FILE *f=argc>1?fopen(argv[1],"wb"):0;
    if(f)wav_hdr(f,total);
    host_tracks_init();song.g[G_BPM]=120;song.rec=1u;transport_req=1;
    for(j=0;j<FS*4u/CTL;j++) {
        mix_block(out,CTL);
        for(k=0;k<CTL;k++)if(f)wav_put(f,out[k*2],out[k*2+1]);
    }
    transport_req=2;events_block(CTL);song.rec=0;
    if(f)fclose(f);
    puts("break studio: the key / note / level maps, every built-in loop distinct, bounded, a stopped hit ends with its slice, metronome (OFF/REC/ON, accent), record: first note starts the loop, at once while playing, latency-compensated steps, free takes set the loop and tempo, BREAK hits recorded from the keys (ghost / hard) and MIDI ch 10 / 4, arm follows the track PASS");
    return 0;
}
