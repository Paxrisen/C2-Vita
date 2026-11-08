// civ2vita — CD-Audio-Ersatz: MCI "cdaudio" spielt Musikdateien aus ux0:data/civ2/MUSIC (ogg/mp3/wav)
// Dateiname muss die Tracknummer enthalten, z. B. Track02.ogg, 03.mp3, Civ2 - 04.wav
#include "rt.h"
#include <mutex>
#include <map>
#include <dirent.h>
#include <psp2/kernel/threadmgr.h>

#define STB_VORBIS_NO_PUSHDATA_API
#include "third/stb_vorbis.c"
#define DR_MP3_IMPLEMENTATION
#include "third/dr_mp3.h"

void post_msg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);

struct Track { std::string path; int kind = 0; int64_t frames = -1; int rate = 44100; };   // kind 1=ogg 2=mp3 3=wav
static std::map<int, Track> g_tracks;
static int g_maxtrack = 0;
static bool g_scanned = false;

static std::mutex g_mm;
static const int RING = 65536;
static int16_t g_ring[RING * 2];
static uint64_t g_rd = 0, g_wr = 0;
static int g_rate = 44100;
static double g_frac = 0;
// Wiedergabezustand
static bool g_open = false, g_playing = false, g_paused = false, g_eof = false, g_finished = false;
static int g_cur = 0, g_last = 0;          // aktueller / letzter Track
static uint64_t g_pos_frames = 0;          // gespielte Frames im aktuellen Track
static int64_t g_stop_frames = -1;         // Ende im letzten Track (-1 = Trackende)
static uint32_t g_notify = 0;              // Fenster fuer MM_MCINOTIFY
static uint32_t g_tfmt = 0;                // 0=ms 2=MSF 10=TMSF
static int g_gen = 0;                      // erhoeht bei jedem Neustart
// Decoder (nur Musikthread)
static stb_vorbis* g_ogg = nullptr; static drmp3 g_mp3; static bool g_mp3_on = false; static FILE* g_wav = nullptr; static int g_wavch = 2, g_wavbits = 16;
static int g_dec_ch = 2;
static bool g_need_open = false; static int g_open_track = 0; static uint64_t g_open_seek = 0;

static void scan() {
    if (g_scanned) return; g_scanned = true;
    std::string dir = g_root + "/MUSIC";
    DIR* d = opendir(dir.c_str()); if (!d) { rlog("CD-Musik: Ordner %s fehlt", dir.c_str()); return; }
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name, l = n; for (auto& ch : l) ch = tolower(ch);
        int kind = 0;
        if (l.size() > 4 && l.compare(l.size() - 4, 4, ".ogg") == 0) kind = 1;
        else if (l.size() > 4 && l.compare(l.size() - 4, 4, ".mp3") == 0) kind = 2;
        else if (l.size() > 4 && l.compare(l.size() - 4, 4, ".wav") == 0) kind = 3;
        if (!kind) continue;
        int num = -1; for (size_t i = 0; i < l.size(); i++) if (isdigit((unsigned char)l[i])) { num = atoi(l.c_str() + i); break; }
        if (num <= 0 || num > 99) continue;
        g_tracks[num] = Track{dir + "/" + n, kind};
        g_maxtrack = std::max(g_maxtrack, num);
    }
    closedir(d);
    rlog("CD-Musik: %d Dateien, hoechster Track %d", (int)g_tracks.size(), g_maxtrack);
}

static void dec_close() {
    if (g_ogg) { stb_vorbis_close(g_ogg); g_ogg = nullptr; }
    if (g_mp3_on) { drmp3_uninit(&g_mp3); g_mp3_on = false; }
    if (g_wav) { fclose(g_wav); g_wav = nullptr; }
}
static bool dec_open(int t, uint64_t seek) {
    dec_close();
    auto it = g_tracks.find(t); if (it == g_tracks.end()) return false;
    Track& tr = it->second;
    if (tr.kind == 1) { int err; g_ogg = stb_vorbis_open_filename(tr.path.c_str(), &err, nullptr); if (!g_ogg) return false;
        stb_vorbis_info in = stb_vorbis_get_info(g_ogg); g_dec_ch = in.channels; tr.rate = in.sample_rate; if (seek) stb_vorbis_seek(g_ogg, (unsigned)seek); }
    else if (tr.kind == 2) { if (!drmp3_init_file(&g_mp3, tr.path.c_str(), nullptr)) return false; g_mp3_on = true;
        g_dec_ch = g_mp3.channels; tr.rate = g_mp3.sampleRate; if (seek) drmp3_seek_to_pcm_frame(&g_mp3, seek); }
    else { g_wav = fopen(tr.path.c_str(), "rb"); if (!g_wav) return false;
        uint8_t h[12]; fread(h, 1, 12, g_wav); bool ok = false;
        while (true) { uint8_t ck[8]; if (fread(ck, 1, 8, g_wav) != 8) break; uint32_t sz; memcpy(&sz, ck + 4, 4);
            if (!memcmp(ck, "fmt ", 4)) { uint8_t f[16]; fread(f, 1, 16, g_wav); g_wavch = f[2] | f[3] << 8; tr.rate = f[4] | f[5] << 8 | f[6] << 16 | f[7] << 24; g_wavbits = f[14] | f[15] << 8; fseek(g_wav, sz - 16 + (sz & 1), SEEK_CUR); }
            else if (!memcmp(ck, "data", 4)) { ok = true; break; } else fseek(g_wav, sz + (sz & 1), SEEK_CUR); }
        if (!ok) { fclose(g_wav); g_wav = nullptr; return false; }
        g_dec_ch = g_wavch; if (seek) fseek(g_wav, (long)(seek * g_wavch * g_wavbits / 8), SEEK_CUR); }
    return true;
}
// liefert gelesene Frames (Stereo int16)
static int dec_read(int16_t* out, int frames) {
    if (g_ogg) return stb_vorbis_get_samples_short_interleaved(g_ogg, 2, out, frames * 2);
    if (g_mp3_on) {
        if (g_dec_ch == 2) return (int)drmp3_read_pcm_frames_s16(&g_mp3, frames, out);
        static int16_t mono[4096]; int n = (int)drmp3_read_pcm_frames_s16(&g_mp3, std::min(frames, 4096), mono);
        for (int i = 0; i < n; i++) out[2 * i] = out[2 * i + 1] = mono[i]; return n; }
    if (g_wav) {
        int bpf = g_wavch * g_wavbits / 8; static uint8_t buf[4096 * 4]; int want = std::min(frames, 4096);
        int n = (int)fread(buf, bpf, want, g_wav);
        for (int i = 0; i < n; i++) for (int c = 0; c < 2; c++) {
            int cc = g_wavch == 2 ? c : 0;
            out[2 * i + c] = g_wavbits == 8 ? (int16_t)((buf[i * bpf + cc] - 128) << 8) : *(int16_t*)(buf + i * bpf + cc * 2); }
        return n; }
    return 0;
}

static int64_t track_frames(int t) {
    auto it = g_tracks.find(t); if (it == g_tracks.end()) return 0;
    Track& tr = it->second; if (tr.frames >= 0) return tr.frames;
    tr.frames = 0;
    if (tr.kind == 1) { int err; stb_vorbis* v = stb_vorbis_open_filename(tr.path.c_str(), &err, nullptr);
        if (v) { tr.frames = stb_vorbis_stream_length_in_samples(v); tr.rate = stb_vorbis_get_info(v).sample_rate; stb_vorbis_close(v); } }
    else if (tr.kind == 2) { drmp3 m; if (drmp3_init_file(&m, tr.path.c_str(), nullptr)) { tr.rate = m.sampleRate; tr.frames = (int64_t)drmp3_get_pcm_frame_count(&m); drmp3_uninit(&m); } }
    else { FILE* f = fopen(tr.path.c_str(), "rb"); if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); tr.frames = (n - 44) / 4; } }
    return tr.frames;
}
static uint32_t track_ms(int t) { track_frames(t); auto it = g_tracks.find(t); if (it == g_tracks.end()) return 0; return (uint32_t)(it->second.frames * 1000 / std::max(1, it->second.rate)); }

static int music_thread(SceSize, void*) {
    static int16_t tmp[4096 * 2];
    for (;;) {
        bool work = false;
        {
            std::unique_lock<std::mutex> lk(g_mm);
            if (g_need_open) {
                g_need_open = false; int t = g_open_track; uint64_t s = g_open_seek; int gen = g_gen;
                lk.unlock(); bool ok = dec_open(t, s); lk.lock();
                if (gen == g_gen) { if (ok) { g_rate = g_tracks[t].rate; g_eof = false; } else { g_eof = true; rlog("CD-Musik: Track %d fehlt", t); } }
            }
            if (g_playing && !g_eof && RING - (int)(g_wr - g_rd) > 4096) {
                int gen = g_gen; lk.unlock();
                int n = dec_read(tmp, 4096);
                lk.lock();
                if (gen == g_gen) {
                    if (n <= 0) {   // Trackende
                        if (g_cur < g_last && g_tracks.count(g_cur + 1)) {
                            // naechster Track erst wenn Puffer leer gespielt (Positionen bleiben stimmig)
                            g_eof = true;
                        } else g_eof = true;
                    } else {
                        for (int i = 0; i < n; i++) { size_t w = (g_wr + i) % RING; g_ring[2 * w] = tmp[2 * i]; g_ring[2 * w + 1] = tmp[2 * i + 1]; }
                        g_wr += n; work = true;
                    }
                }
            }
        }
        sceKernelDelayThread(work ? 1000 : 10000);
    }
    return 0;
}

static void start_track(int t, uint64_t seekframes) {
    g_gen++; g_rd = g_wr = 0; g_frac = 0; g_cur = t; g_pos_frames = seekframes; g_eof = false; g_finished = false;
    g_need_open = true; g_open_track = t; g_open_seek = seekframes;
}

// Audio-Thread: in Mischpuffer addieren
void cd_mix(float* acc, int n, int outrate) {
    std::lock_guard<std::mutex> lk(g_mm);
    if (!g_playing || g_paused) return;
    double step = (double)g_rate / outrate;
    for (int i = 0; i < n; i++) {
        if (g_wr - g_rd < 2) {
            if (g_eof && !g_need_open) {
                if (g_cur < g_last) { start_track(g_cur + 1, 0); }
                else { g_playing = false; g_finished = true; }
            }
            return;
        }
        if (g_stop_frames >= 0 && g_cur == g_last && (int64_t)g_pos_frames >= g_stop_frames) { g_playing = false; g_finished = true; return; }
        size_t a = g_rd % RING, b = (g_rd + 1) % RING; float fr = (float)g_frac;
        acc[2 * i] += (g_ring[2 * a] * (1 - fr) + g_ring[2 * b] * fr) / 32768.0f;
        acc[2 * i + 1] += (g_ring[2 * a + 1] * (1 - fr) + g_ring[2 * b + 1] * fr) / 32768.0f;
        g_frac += step; uint32_t adv = (uint32_t)g_frac; g_frac -= adv; g_rd += adv; g_pos_frames += adv;
    }
}
// Hauptthread: Benachrichtigung senden
void cd_poll() {
    uint32_t hw = 0;
    { std::lock_guard<std::mutex> lk(g_mm); if (g_finished) { g_finished = false; hw = g_notify; g_notify = 0; } }
    if (hw) post_msg(hw, 0x3B9, 1, 1);   // MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, Geraet 1
}

static uint32_t enc_time(int track, uint32_t ms) {
    uint32_t m = ms / 60000, s = ms / 1000 % 60, f = ms % 1000 * 75 / 1000;
    if (g_tfmt == 10) return track | m << 8 | s << 16 | f << 24;
    if (g_tfmt == 2) return m | s << 8 | f << 16;
    return ms;
}
static void dec_time(uint32_t v, int& track, uint32_t& ms) {
    if (g_tfmt == 10) { track = v & 0xFF; ms = ((v >> 8 & 0xFF) * 60 + (v >> 16 & 0xFF)) * 1000 + (v >> 24 & 0xFF) * 1000 / 75; }
    else if (g_tfmt == 2) { uint32_t abs = ((v & 0xFF) * 60 + (v >> 8 & 0xFF)) * 1000 + (v >> 16 & 0xFF) * 1000 / 75;   // absolut: Tracks summieren
        track = 2; ms = abs; while (g_tracks.count(track) && ms >= track_ms(track) && track < g_maxtrack) { ms -= track_ms(track); track++; } }
    else { track = 2; ms = v; while (ms >= track_ms(track) && track < g_maxtrack) { ms -= track_ms(track); track++; } }
}

static bool g_thread = false;
// Rueckgabe: MCI-Fehlercode; *handled=false wenn nicht unser Geraet
uint32_t cd_mci(uint32_t dev, uint32_t cmd, uint32_t fl, uint32_t p, bool& handled) {
    handled = false;
    if (cmd == 0x803) {   // MCI_OPEN
        bool cd = false;
        if (fl & 0x1000) cd = (rd32(p + 8) & 0xFFFF) == 516;                         // MCI_OPEN_TYPE_ID: MCI_DEVTYPE_CD_AUDIO
        else if (fl & 0x2000) { const char* t = GS(rd32(p + 8)); cd = t && !strcasecmp(t, "cdaudio"); }
        if (!cd) return 0;
        handled = true; scan();
        if (g_tracks.empty()) { rlog("mciSendCommand(MCI_OPEN cdaudio) -> keine Musikdateien in MUSIC"); return 266; }
        if (!g_thread) { g_thread = true; SceUID t = sceKernelCreateThread("civ2_music", music_thread, 100, 0x10000, 0, 0x40000 /* Kern 2 */, nullptr); if (t >= 0) sceKernelStartThread(t, 0, nullptr); }
        g_open = true; wr32(p + 4, 1);
        rlog("mciSendCommand(MCI_OPEN cdaudio) -> Geraet 1 (%d Tracks)", g_maxtrack);
        return 0;
    }
    if (dev != 1 || !g_open) return 0;
    handled = true;
    if (cmd == 0x814 && rd32(p + 8) == 1) {   // Laengen ausserhalb der Sperre berechnen (kann dauern)
        if (fl & 0x10) track_ms(rd32(p + 12)); else for (auto& kv : g_tracks) track_ms(kv.first); }
    std::lock_guard<std::mutex> lk(g_mm);
    uint32_t cb = (fl & 1) ? rd32(p) : 0;
    switch (cmd) {
    case 0x804: g_playing = false; g_open = false; g_gen++; return 0;                    // CLOSE
    case 0x80D: if (fl & 0x400) g_tfmt = rd32(p + 4); return 0;                         // SET
    case 0x806: {                                                                         // PLAY
        int ft = g_cur ? g_cur : 2; uint32_t fms = 0;
        if (fl & 4) dec_time(rd32(p + 4), ft, fms);
        else if (!g_playing) fms = (uint32_t)(g_pos_frames * 1000 / std::max(1, g_rate));
        int lt = g_maxtrack; g_stop_frames = -1;
        if (fl & 8) { int tt; uint32_t tms; dec_time(rd32(p + 8), tt, tms);
            if (tms == 0 && tt > ft) lt = tt - 1; else { lt = tt; g_stop_frames = (int64_t)tms * std::max(1, g_tracks.count(tt) ? g_tracks[tt].rate : 44100) / 1000; } }
        if (g_notify && g_notify != cb) post_msg(g_notify, 0x3B9, 2, 1);                  // SUPERSEDED
        g_notify = cb; g_last = lt; g_playing = true; g_paused = false;
        int rate = g_tracks.count(ft) ? g_tracks[ft].rate : 44100;
        start_track(ft, (uint64_t)fms * rate / 1000);
        rlog("CD-Musik: spiele Track %d (bis %d)", ft, lt);
        return 0; }
    case 0x808:                                                                           // STOP
        if (g_notify) { post_msg(g_notify, 0x3B9, 4, 1); g_notify = 0; }
        g_playing = false; g_gen++; return 0;
    case 0x809: g_paused = true; return 0;                                                // PAUSE
    case 0x855: g_paused = false; return 0;                                               // RESUME
    case 0x807: {                                                                         // SEEK
        g_playing = false; g_gen++;
        if (fl & 0x100) { g_cur = 2; g_pos_frames = 0; } else if (fl & 0x200) { g_cur = g_maxtrack; g_pos_frames = 0; }
        else if (fl & 8) { int t; uint32_t ms; dec_time(rd32(p + 8), t, ms); g_cur = t; g_pos_frames = (uint64_t)ms * 44100 / 1000; }
        return 0; }
    case 0x814: {                                                                         // STATUS
        uint32_t item = rd32(p + 8), r = 0;
        int trk = (fl & 0x10) ? (int)rd32(p + 12) : 0;
        switch (item) {
            case 1: { uint32_t ms = 0;
                    if (trk) ms = track_ms(trk); else for (auto& kv : g_tracks) ms += track_ms(kv.first);
                    if (g_tfmt == 10 || g_tfmt == 2) { uint32_t m = ms / 60000, sec = ms / 1000 % 60, f = ms % 1000 * 75 / 1000; r = m | sec << 8 | f << 16; } else r = ms;
                    break; }
            case 2: if (trk) r = enc_time(trk, 0); else r = enc_time(g_cur ? g_cur : 2, (uint32_t)(g_pos_frames * 1000 / std::max(1, g_rate))); break;
            case 3: {   // Civ2 erkennt seine CDs an der Trackanzahl (10 = Original, 12 = Gold, 18 = Szenarien, 24 = Fantastische Welten)
                static const int known[4] = {10, 12, 18, 24}; r = 24;
                for (int k : known) if (g_maxtrack <= k) { r = k; break; }
                break; }
            case 4: r = g_playing ? (g_paused ? 529 : 526) : 525; break;
            case 5: case 7: r = 1; break;
            case 6: r = g_tfmt; break;
            case 8: r = g_cur ? g_cur : 1; break;
            case 0x4001: r = (trk >= 2 && g_tracks.count(trk)) ? 1088 : 1089; break;   // Audio- / Datentrack
            default: rlog("CD-Musik: STATUS %x unbekannt", item); break;
        }
        wr32(p + 4, r); return 0; }
    case 0x80B: { uint32_t item = rd32(p + 8); wr32(p + 4, item == 4 ? 516 : 1); return 0; }   // GETDEVCAPS
    case 0x80A: if (rd32(p + 4) && rd32(p + 8)) { strncpy(GS(rd32(p + 4)), "Civ2 Musik", rd32(p + 8)); } return 0;   // INFO
    default: rlog("CD-Musik: MCI-Befehl %x (Flags %x) ignoriert", cmd, fl); return 0;
    }
}
