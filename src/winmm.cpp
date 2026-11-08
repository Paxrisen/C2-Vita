// civ2vita — WINMM: Zeit, Multimedia-Timer, waveOut (Software-Mixer auf sceAudioOut), mmio, sndPlaySound, MCI
#include "rt.h"
#include <mutex>
#include <deque>
#include <map>
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

extern std::vector<uint8_t> slurp(const std::string& p);
void post_msg(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp);

// ============================================================ Mixer
struct WaveDev { uint32_t h; int ch = 1, bits = 8, rate = 22050; uint32_t cbtype = 0, cb = 0, inst = 0;
    std::deque<uint32_t> q; uint32_t curpos = 0; double frac = 0; float lp[2] = {0, 0}; uint64_t played = 0; bool paused = false; };
struct SndVoice { std::vector<uint8_t> pcm; int ch = 1, bits = 8, rate = 22050; size_t pos = 0; double frac = 0; bool loop = false, on = false; };
static std::mutex g_amx;
static std::map<uint32_t, WaveDev> g_wave;
static SndVoice g_snd;
struct DoneEv { uint32_t dev, hdr; };
static std::vector<DoneEv> g_done;
static int g_port = -1;
extern int g_volume;
void cd_mix(float* acc, int n, int outrate); void cd_poll();
uint32_t cd_mci(uint32_t dev, uint32_t cmd, uint32_t fl, uint32_t p, bool& handled);
static bool g_noaudio = false;
// Ohne Tonausgabe: Puffer nach ihrer Spieldauer als fertig melden, damit das Spiel nie wartet
static void fake_consume() {
    static uint64_t last = 0; uint64_t t = now_us(); if (!last) last = t;
    uint64_t dt = t - last; last = t;
    std::lock_guard<std::mutex> lk(g_amx);
    for (auto& kv : g_wave) { WaveDev& d = kv.second; if (d.paused) continue;
        int bpf = d.ch * d.bits / 8; double frames = (double)d.rate * dt / 1e6;
        d.frac += frames;
        while (!d.q.empty()) { uint32_t hdr = d.q.front(); uint32_t len = rd32(hdr + 4) / (bpf ? bpf : 1);
            uint32_t need = len - d.curpos; if (d.frac < need) { d.curpos += (uint32_t)d.frac; d.frac -= (uint32_t)d.frac; break; }
            d.frac -= need; d.q.pop_front(); d.curpos = 0; wr32(hdr + 16, (rd32(hdr + 16) & ~0x10u) | 1u); g_done.push_back({kv.first, hdr}); }
        if (d.q.empty()) d.frac = 0; }
}
static const int GRAIN = 512, OUTRATE = 48000;

static inline float sample(const uint8_t* p, int bits, int ch, int c) {
    if (bits == 8) return ((int)p[ch == 2 ? c : 0] - 128) / 128.0f;
    const int16_t* s = (const int16_t*)p; return s[ch == 2 ? c : 0] / 32768.0f;
}
static int audio_thread(SceSize, void*) {
    static int16_t outbuf[2][GRAIN * 2]; static float acc[GRAIN * 2]; int cur = 0;
    for (;;) {
        int16_t* out = outbuf[cur]; cur ^= 1;   // Doppelpuffer: die Hardware liest den vorigen Puffer noch
        std::fill(acc, acc + GRAIN * 2, 0.0f);
        {
            std::lock_guard<std::mutex> lk(g_amx);
            for (auto& kv : g_wave) {
                WaveDev& d = kv.second; if (d.paused) continue;
                int bpf = d.ch * d.bits / 8; double step = (double)d.rate / OUTRATE;
                for (int i = 0; i < GRAIN && !d.q.empty(); ) {
                    uint32_t hdr = d.q.front();
                    uint32_t data = rd32(hdr), len = rd32(hdr + 4);
                    uint32_t frames = bpf ? len / bpf : 0;
                    if (d.curpos >= frames) {
                        d.q.pop_front(); d.curpos = 0;
                        wr32(hdr + 16, (rd32(hdr + 16) & ~0x10u) | 1u);
                        g_done.push_back({kv.first, hdr}); continue;
                    }
                    const uint8_t* p = (const uint8_t*)G(data + d.curpos * bpf);
                    const uint8_t* q = (d.curpos + 1 < frames) ? p + bpf : p;
                    float fr = (float)d.frac;
                    float l = sample(p, d.bits, d.ch, 0) * (1 - fr) + sample(q, d.bits, d.ch, 0) * fr;
                    float r = sample(p, d.bits, d.ch, 1) * (1 - fr) + sample(q, d.bits, d.ch, 1) * fr;
                    if (d.bits == 8) { d.lp[0] += (l - d.lp[0]) * 0.55f; d.lp[1] += (r - d.lp[1]) * 0.55f; l = d.lp[0]; r = d.lp[1]; }   // 8-Bit-Rauschen glaetten
                    acc[2 * i] += l; acc[2 * i + 1] += r;
                    d.frac += step; uint32_t adv = (uint32_t)d.frac; d.frac -= adv; d.curpos += adv; d.played += adv * bpf;
                    i++;
                }
            }
            cd_mix(acc, GRAIN, OUTRATE);
            if (g_snd.on) {
                SndVoice& v = g_snd; int bpf = v.ch * v.bits / 8; size_t frames = bpf ? v.pcm.size() / bpf : 0; double step = (double)v.rate / OUTRATE;
                for (int i = 0; i < GRAIN && v.on; i++) {
                    if (v.pos >= frames) { if (v.loop && frames) v.pos = 0; else { v.on = false; break; } }
                    const uint8_t* p = v.pcm.data() + v.pos * bpf;
                    acc[2 * i] += sample(p, v.bits, v.ch, 0); acc[2 * i + 1] += sample(p, v.bits, v.ch, 1);
                    v.frac += step; size_t adv = (size_t)v.frac; v.frac -= adv; v.pos += adv;
                }
            }
        }
        for (int i = 0; i < GRAIN * 2; i++) {
            float f = acc[i] * (g_volume / 100.0f);
            float a = fabsf(f); if (a > 0.9f) { float e = (a - 0.9f) / 0.1f; a = 0.9f + 0.1f * e / (1.0f + e); f = f < 0 ? -a : a; }   // weicher Begrenzer statt hartem Abschneiden
            out[i] = (int16_t)(f * 32767); }
        sceAudioOutOutput(g_port, out);
    }
    return 0;
}
static void audio_init() {
    g_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, OUTRATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (g_port < 0) { rlog("Audio: Port-Fehler %08x", g_port); g_noaudio = true; return; }
    int vol[2] = { 32768, 32768 }; sceAudioOutSetVolume(g_port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
    SceUID t = sceKernelCreateThread("civ2_audio", audio_thread, 96, 0x10000, 0, 0x20000 /* Kern 1 */, nullptr);
    if (t < 0) t = sceKernelCreateThread("civ2_audio", audio_thread, 0x10000100, 0x10000, 0, 0, nullptr);
    int sr = t >= 0 ? sceKernelStartThread(t, 0, nullptr) : -1;
    if (t < 0 || sr < 0) g_noaudio = true;
    rlog("Audio: Port %d, Thread %08x, Start %08x", g_port, (unsigned)t, (unsigned)sr);
}

// ============================================================ Timer
struct MMTimer { uint32_t id, delay, proc, user, flags; uint32_t next; };
static std::vector<MMTimer> g_mmt;
static bool g_in_timers = false;
void timers_poll() {
    if (g_in_timers) return;
    { static uint64_t last = 0; uint64_t t = now_us(); if (t - last < 1000) return; last = t; }
    g_in_timers = true;
    if (g_noaudio) fake_consume();
    cd_poll();
    std::vector<DoneEv> done;
    { std::lock_guard<std::mutex> lk(g_amx); done.swap(g_done); }
    for (auto& d : done) {
        WaveDev dev; bool ok = false;
        { std::lock_guard<std::mutex> lk(g_amx); auto it = g_wave.find(d.dev); if (it != g_wave.end()) { dev.cbtype = it->second.cbtype; dev.cb = it->second.cb; dev.inst = it->second.inst; ok = true; } }
        if (!ok) continue;
        if (dev.cbtype == 0x10000) post_msg(dev.cb, 0x3BD, d.dev, d.hdr);
        else if (dev.cbtype == 0x30000) gcall(dev.cb, { d.dev, 0x3BD, dev.inst, d.hdr, 0 });
    }
    uint32_t t = now_ms();
    for (size_t i = 0; i < g_mmt.size(); i++) {
        if ((int32_t)(t - g_mmt[i].next) < 0) continue;
        MMTimer tm = g_mmt[i];
        if (tm.flags & 1) g_mmt[i].next = t + (tm.delay ? tm.delay : 1);
        else { g_mmt.erase(g_mmt.begin() + i); i--; }
        if (tm.flags & 0x10) { /* TIME_CALLBACK_EVENT_SET: ignorieren */ continue; }
        static uint32_t tdcb = g_br->shim_trap("TIMERDLL.dll", "TimerCallBack");
        extern void timerdll_callback(uint32_t, uint32_t);
        if (tm.proc == tdcb) timerdll_callback(tm.id, tm.user); else gcall(tm.proc, { tm.id, 0, tm.user, 0, 0 });
    }
    g_in_timers = false;
}

// ============================================================ WAV laden
static bool parse_wav(const uint8_t* d, size_t n, SndVoice& v) {
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return false;
    size_t p = 12; bool fmt = false;
    while (p + 8 <= n) {
        uint32_t sz; memcpy(&sz, d + p + 4, 4);
        if (!memcmp(d + p, "fmt ", 4)) { uint16_t ch, bits; uint32_t rate; memcpy(&ch, d + p + 10, 2); memcpy(&rate, d + p + 12, 4); memcpy(&bits, d + p + 22, 2);
            v.ch = ch; v.rate = rate; v.bits = bits; fmt = true; }
        if (!memcmp(d + p, "data", 4) && fmt) { size_t e = std::min(n, p + 8 + sz); v.pcm.assign(d + p + 8, d + e); return true; }
        p += 8 + sz + (sz & 1);
    }
    return false;
}

// ============================================================ mmio
struct MMIOF { int fd = -1; uint32_t buf = 0; uint32_t bufsize = 0x4000; };
static std::map<uint32_t, MMIOF> g_mmio;
static uint32_t g_mmnext = 0x00900010;

void install_winmm() {
    audio_init();
    const char* M = "WINMM.dll";
    reg(M, "timeGetTime", 0, [](Cpu&) { timers_poll(); return now_ms(); });
    reg(M, "timeBeginPeriod", 1, [](Cpu&) { return 0u; });
    reg(M, "timeEndPeriod", 1, [](Cpu&) { return 0u; });
    reg(M, "timeGetDevCaps", 2, [](Cpu& c) { wr32(A(0), 1); wr32(A(0) + 4, 1000000); return 0u; });
    reg(M, "timeSetEvent", 5, [](Cpu& c) { static uint32_t id = 1; rlog("timeSetEvent(%u ms, proc=%08x, flags=%x)", A(0), A(2), A(4)); MMTimer t{id++, A(0), A(2), A(3), A(4), now_ms() + A(0)}; g_mmt.push_back(t); return t.id; });
    reg(M, "timeKillEvent", 1, [](Cpu& c) { auto it = std::remove_if(g_mmt.begin(), g_mmt.end(), [&](const MMTimer& t) { return t.id == A(0); }); bool f = it != g_mmt.end(); g_mmt.erase(it, g_mmt.end()); return f ? 0u : 97u; });
    // ---- waveOut
    reg(M, "waveOutGetNumDevs", 0, [](Cpu&) { return 1u; });
    reg(M, "waveOutGetDevCapsA", 3, [](Cpu& c) { uint32_t p = A(1); memset(G(p), 0, std::min<uint32_t>(A(2), 52)); wr16(p, 1); wr16(p + 2, 1); wr32(p + 4, 0x400);
        strcpy(GS(p + 8), "PS Vita Audio"); wr32(p + 40, 0xFFF); wr16(p + 44, 2); wr32(p + 48, 0x0C); return 0u; });
    reg(M, "waveOutGetErrorTextA", 3, [](Cpu& c) { if (A(1) && A(2)) { strncpy(GS(A(1)), "Audiofehler", A(2)); GS(A(1))[A(2) - 1] = 0; } return 0u; });
    reg(M, "waveOutOpen", 6, [](Cpu& c) -> uint32_t {
        if (g_port < 0) g_noaudio = true;
        uint32_t fx = A(2); int tag = rd16(fx), ch = rd16(fx + 2), rate = rd32(fx + 4), bits = rd16(fx + 14);
        if (tag != 1 || (bits != 8 && bits != 16) || ch < 1 || ch > 2) return 32;  // WAVERR_BADFORMAT
        if (A(5) & 1) return 0;   // WAVE_FORMAT_QUERY
        static uint32_t hn = 0x00910010; uint32_t h = hn; hn += 4;
        WaveDev d; d.h = h; d.ch = ch; d.bits = bits; d.rate = rate; d.cbtype = A(5) & 0x70000; d.cb = A(3); d.inst = A(4);
        { std::lock_guard<std::mutex> lk(g_amx); g_wave[h] = d; }
        if (A(0)) wr32(A(0), h);
        if (d.cbtype == 0x10000) post_msg(d.cb, 0x3BB, h, 0); else if (d.cbtype == 0x30000) gcall(d.cb, { h, 0x3BB, d.inst, 0, 0 });
        rlog("waveOutOpen %d Hz %d bit %d Kanaele (tag %d, block %d, %d B/s, cb %x)", rate, bits, ch, tag, rd16(fx + 12), rd32(fx + 8), d.cbtype);
        return 0; });
    reg(M, "waveOutClose", 1, [](Cpu& c) {
        WaveDev d; { std::lock_guard<std::mutex> lk(g_amx); auto it = g_wave.find(A(0)); if (it == g_wave.end()) return 5u; d = it->second; g_wave.erase(it); }
        if (d.cbtype == 0x10000) post_msg(d.cb, 0x3BC, A(0), 0); else if (d.cbtype == 0x30000) gcall(d.cb, { A(0), 0x3BC, d.inst, 0, 0 });
        return 0u; });
    reg(M, "waveOutPrepareHeader", 3, [](Cpu& c) { wr32(A(1) + 16, rd32(A(1) + 16) | 2); return 0u; });
    reg(M, "waveOutUnprepareHeader", 3, [](Cpu& c) { if (rd32(A(1) + 16) & 0x10) return 33u; wr32(A(1) + 16, rd32(A(1) + 16) & ~2u); return 0u; });
    reg(M, "waveOutWrite", 3, [](Cpu& c) {
        std::lock_guard<std::mutex> lk(g_amx); auto it = g_wave.find(A(0)); if (it == g_wave.end()) return 5u;
        static int nlog = 0; if (nlog < 12) { nlog++; rlog("waveOutWrite len=%u flags=%x loops=%u q=%u", rd32(A(1) + 4), rd32(A(1) + 16), rd32(A(1) + 20), (unsigned)it->second.q.size()); }
        wr32(A(1) + 16, (rd32(A(1) + 16) & ~1u) | 0x10); it->second.q.push_back(A(1)); return 0u; });
    reg(M, "waveOutReset", 1, [](Cpu& c) {
        std::lock_guard<std::mutex> lk(g_amx); auto it = g_wave.find(A(0)); if (it == g_wave.end()) return 5u;
        for (uint32_t h : it->second.q) { wr32(h + 16, (rd32(h + 16) & ~0x10u) | 1u); g_done.push_back({A(0), h}); }
        it->second.q.clear(); it->second.curpos = 0; it->second.played = 0; return 0u; });
    reg(M, "waveOutPause", 1, [](Cpu& c) { std::lock_guard<std::mutex> lk(g_amx); auto it = g_wave.find(A(0)); if (it != g_wave.end()) it->second.paused = true; return 0u; });
    reg(M, "waveOutRestart", 1, [](Cpu& c) { std::lock_guard<std::mutex> lk(g_amx); auto it = g_wave.find(A(0)); if (it != g_wave.end()) it->second.paused = false; return 0u; });
    reg(M, "waveOutGetPosition", 3, [](Cpu& c) {
        std::lock_guard<std::mutex> lk(g_amx); auto it = g_wave.find(A(0)); if (it == g_wave.end()) return 5u;
        uint32_t t = rd32(A(1)); WaveDev& d = it->second; uint64_t b = d.played; int bpf = d.ch * d.bits / 8;
        if (t == 2) wr32(A(1) + 4, (uint32_t)(b / bpf)); else if (t == 1) wr32(A(1) + 4, (uint32_t)(b * 1000 / (bpf * d.rate))); else { wr32(A(1), 4); wr32(A(1) + 4, (uint32_t)b); }
        return 0u; });
    reg(M, "waveOutSetVolume", 2, [](Cpu&) { return 0u; });
    reg(M, "waveOutGetVolume", 2, [](Cpu& c) { if (A(1)) wr32(A(1), 0xFFFFFFFF); return 0u; });
    // ---- sndPlaySound / PlaySound
    auto play = [](uint32_t name, uint32_t flags) -> uint32_t {
        std::lock_guard<std::mutex> lk(g_amx);
        if (!name) { g_snd.on = false; return 1; }
        SndVoice v;
        if (flags & 4) { uint32_t sz = rd32(name + 4) + 8; if (!parse_wav((const uint8_t*)G(name), sz, v)) return 0; }
        else { auto b = slurp(win2host(GS(name))); if (b.empty() || !parse_wav(b.data(), b.size(), v)) { rlog("sndPlaySound: %s fehlt", GS(name)); return 0; } }
        if ((flags & 0x10) && g_snd.on) return 0;
        { static int sn = 0; if (sn < 30) { sn++; rlog("sndPlaySound %s (%d Hz %d bit %d K, %u B, flags %x)", (flags & 4) ? "<Speicher>" : GS(name), v.rate, v.bits, v.ch, (unsigned)v.pcm.size(), flags); } }
        v.loop = (flags & 8) != 0; v.on = true; g_snd = std::move(v);
        return 1; };
    reg(M, "sndPlaySoundA", 2, [play](Cpu& c) { return play(A(0), A(1)); });
    reg(M, "PlaySoundA", 3, [play](Cpu& c) { return play(A(0), A(2)); });
    // ---- MCI (CD-Audio/MIDI): vorerst nicht verfuegbar
    reg(M, "mciSendCommandA", 4, [](Cpu& c) {
        bool h; uint32_t r = cd_mci(A(0), A(1), A(2), A(3), h);
        if (h) return r;
        if (A(1) == 0x803) { rlog("mciSendCommand(MCI_OPEN) -> nicht verfuegbar"); return 266u; }
        return 0u; });
    reg(M, "mciSendStringA", 4, [](Cpu& c) { rlog("mciSendString(%s)", GS(A(0))); return 266u; });
    reg(M, "mciGetDeviceIDA", 1, [](Cpu&) { return 0u; });
    reg(M, "mciGetErrorStringA", 3, [](Cpu& c) { if (A(1) && A(2)) { strncpy(GS(A(1)), "Geraet nicht verfuegbar", A(2)); GS(A(1))[A(2) - 1] = 0; } return 1u; });
    reg(M, "midiOutGetNumDevs", 0, [](Cpu&) { return 0u; });
    reg(M, "auxGetNumDevs", 0, [](Cpu&) { return 0u; });
    // ---- mmio
    reg(M, "mmioOpenA", 3, [](Cpu& c) -> uint32_t {
        uint32_t fl = A(2); std::string p = win2host(GS(A(0)));
        if (fl & 0x4000) { int fd = open(p.c_str(), O_RDONLY); if (fd < 0) return 0; close(fd); return 1; }
        int of = (fl & 3) == 1 ? (O_WRONLY | O_CREAT | O_TRUNC) : (fl & 3) == 2 ? (O_RDWR | O_CREAT) : O_RDONLY;
        if (fl & 0x1000) of |= O_CREAT | O_TRUNC;
        int fd = open(p.c_str(), of, 0666);
        if (fd < 0) { if (A(1)) wr16(A(1) + 12, 257); rlog("mmioOpen %s fehlt", GS(A(0))); return 0; }
        uint32_t h = g_mmnext; g_mmnext += 4; MMIOF m; m.fd = fd; m.buf = galloc(m.bufsize); g_mmio[h] = m;
        if (A(1)) wr16(A(1) + 12, 0);
        return h; });
    reg(M, "mmioClose", 2, [](Cpu& c) { auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 0u; close(it->second.fd); gfree(it->second.buf); g_mmio.erase(it); return 0u; });
    reg(M, "mmioRead", 3, [](Cpu& c) { auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 0xFFFFFFFFu; int r = read(it->second.fd, G(A(1)), A(2)); return (uint32_t)r; });
    reg(M, "mmioWrite", 3, [](Cpu& c) { auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 0xFFFFFFFFu; int r = write(it->second.fd, G(A(1)), A(2)); return (uint32_t)r; });
    reg(M, "mmioSeek", 3, [](Cpu& c) { auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 0xFFFFFFFFu; return (uint32_t)lseek(it->second.fd, (int32_t)A(1), A(2)); });
    reg(M, "mmioDescend", 4, [](Cpu& c) -> uint32_t {
        auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 265;
        int fd = it->second.fd; uint32_t ck = A(1), par = A(2), fl = A(3);
        uint32_t end = 0xFFFFFFFF; if (par) end = rd32(par + 12) + rd32(par + 4);
        uint32_t wantId = rd32(ck), wantType = rd32(ck + 8);
        for (;;) {
            off_t pos = lseek(fd, 0, SEEK_CUR); if ((uint32_t)pos + 8 > end) return 265;
            uint32_t hdr[3]; if (read(fd, hdr, 8) != 8) return 265;
            uint32_t id = hdr[0], sz = hdr[1], type = 0;
            bool isList = id == 0x46464952u || id == 0x5453494Cu;   // RIFF / LIST
            if (isList) { if (read(fd, &type, 4) != 4) return 265; }
            bool match = true;
            if (fl & 0x10) match = id == wantId;
            else if (fl & 0x20) match = id == 0x46464952u && type == wantType;
            else if (fl & 0x40) match = id == 0x5453494Cu && type == wantType;
            if (match) { wr32(ck, id); wr32(ck + 4, sz); wr32(ck + 8, type); wr32(ck + 12, (uint32_t)pos + 8); wr32(ck + 16, 0); return 0; }
            lseek(fd, pos + 8 + sz + (sz & 1), SEEK_SET);
        } });
    reg(M, "mmioAscend", 3, [](Cpu& c) {
        auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 265u;
        uint32_t ck = A(1), sz = rd32(ck + 4); lseek(it->second.fd, rd32(ck + 12) + sz + (sz & 1), SEEK_SET); return 0u; });
    auto fillinfo = [](uint32_t h, MMIOF& m, uint32_t info) {
        off_t pos = lseek(m.fd, 0, SEEK_CUR);
        int r = read(m.fd, G(m.buf), m.bufsize); if (r < 0) r = 0;
        lseek(m.fd, pos, SEEK_SET);
        memset(G(info), 0, 72);
        wr32(info + 20, m.bufsize); wr32(info + 24, m.buf); wr32(info + 28, m.buf); wr32(info + 32, m.buf + r); wr32(info + 36, m.buf + m.bufsize);
        wr32(info + 40, (uint32_t)pos); wr32(info + 44, (uint32_t)pos + r); wr32(info + 68, h); };
    reg(M, "mmioGetInfo", 3, [fillinfo](Cpu& c) { auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 5u; fillinfo(A(0), it->second, A(1)); return 0u; });
    reg(M, "mmioSetInfo", 3, [](Cpu& c) {
        auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 5u; uint32_t info = A(1);
        uint32_t used = rd32(info + 28) - rd32(info + 24); lseek(it->second.fd, rd32(info + 40) + used, SEEK_SET); return 0u; });
    reg(M, "mmioAdvance", 3, [fillinfo](Cpu& c) {
        auto it = g_mmio.find(A(0)); if (it == g_mmio.end()) return 5u; uint32_t info = A(1);
        uint32_t used = rd32(info + 28) - rd32(info + 24); lseek(it->second.fd, rd32(info + 40) + used, SEEK_SET);
        fillinfo(A(0), it->second, info); return 0u; });
}
