// civ2vita — COMDLG32 (eigene Dateiauswahl), AVIFIL32/MSVFW32/DDRAW (nicht verfuegbar), XDaemon (Mehrspieler aus), COMCTL32
#include "win.h"
#include <dirent.h>
#include <sys/stat.h>

static bool wild(const char* p, const char* s) {
    if (!*p) return !*s;
    if (*p == '*') { for (;;) { if (wild(p + 1, s)) return true; if (!*s) return false; s++; } }
    if (!*s) return false;
    if (*p == '?' || toupper((uint8_t)*p) == toupper((uint8_t)*s)) return wild(p + 1, s + 1);
    return false;
}
static bool match_any(const std::string& pats, const char* name) {
    size_t a = 0;
    while (a <= pats.size()) { size_t b = pats.find(';', a); if (b == std::string::npos) b = pats.size();
        std::string p = pats.substr(a, b - a); while (!p.empty() && p[0] == ' ') p.erase(0, 1);
        if (p == "*.*" || wild(p.c_str(), name)) return true; a = b + 1; }
    return false;
}

// Dateiauswahl -> voller Windows-Pfad oder leer
static std::string file_picker(const char* title, std::string dirw, const std::string& pats, bool save, const std::string& defname, const char* defext) {
    for (;;) {
        struct Ent { std::string name; bool dir; };
        std::vector<Ent> ents;
        std::string host = win2host(dirw.c_str());
        if (dirw.size() > 3) ents.push_back({"..", true});
        if (DIR* d = opendir(host.c_str())) {
            std::vector<Ent> dirs, files;
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name; if (n == "." || n == "..") continue;
                struct stat st; bool isdir = stat((host + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
                if (isdir) { if (n != "fonts") dirs.push_back({n, true}); }
                else if (match_any(pats, n.c_str())) files.push_back({n, false});
            }
            closedir(d);
            auto cmp = [](const Ent& a, const Ent& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; };
            std::sort(dirs.begin(), dirs.end(), cmp); std::sort(files.begin(), files.end(), cmp);
            ents.insert(ents.end(), dirs.begin(), dirs.end()); ents.insert(ents.end(), files.begin(), files.end());
        }
        if (save) ents.insert(ents.begin(), {TR("<Neuer Dateiname...>", "<New file name...>"), false});
        Rect r{(SCR_W - 560) / 2, 30, (SCR_W + 560) / 2, SCR_H - 30};
        int ih = 20; Rect lr{r.l + 10, r.t + 50, r.r - 10, r.b - 50}; int rows = lr.h() / ih;
        int sel = 0, top = 0, n = ents.size();
        if (save) sel = 0; else if (n > (dirw.size() > 3 ? 1 : 0)) sel = dirw.size() > 3 ? 1 : 0;
        Rect bOk{r.r - 210, r.b - 38, r.r - 115, r.b - 12}, bCa{r.r - 105, r.b - 38, r.r - 10, r.b - 12};
        ScreenSave sv; screen_save(sv, r);
        auto draw = [&]() {
            scr_fill(r, 0xC0C0C0); scr_frame3d(r, true);
            Rect c{r.l + 3, r.t + 3, r.r - 3, r.t + 22}; scr_fill(c, 0x800000); scr_text(c, title, 0xFFFFFF, 0, true);
            scr_text({r.l + 10, r.t + 26, r.r - 10, r.t + 46}, dirw.c_str(), 0, 0);
            scr_fill(lr, 0xFFFFFF); scr_frame3d({lr.l - 1, lr.t - 1, lr.r + 1, lr.b + 1}, false);
            for (int i = 0; i < rows && top + i < n; i++) { Rect ir{lr.l, lr.t + i * ih, lr.r, lr.t + (i + 1) * ih};
                bool s = top + i == sel; if (s) scr_fill(ir, 0x800000);
                std::string t = ents[top + i].dir ? "[" + ents[top + i].name + "]" : ents[top + i].name;
                scr_text(ir, t.c_str(), s ? 0xFFFFFF : 0, 0); }
            for (auto* b : {&bOk, &bCa}) { scr_fill(*b, 0xC0C0C0); scr_frame3d(*b, true); }
            scr_text(bOk, save ? TR("Speichern (X)", "Save (X)") : TR("Oeffnen (X)", "Open (X)"), 0, 1); scr_text(bCa, TR("Abbrechen (O)", "Cancel (O)"), 0, 1);
            if (!n) scr_text(lr, TR("(keine passenden Dateien)", "(no matching files)"), 0x808080, 1);
        };
        draw(); plat_present(true);
        int act = 0; int lastTap = -1; uint32_t lastT = 0; int dragY = -1, dragTop = 0;
        while (!act) {
            plat_poll(); InputEv e;
            while (plat_next_event(e) && !act) {
                if (e.type == EV_LDOWN) { if (lr.has(e.x, e.y)) { dragY = e.y; dragTop = top; } }
                if (e.type == EV_MOUSEMOVE && dragY >= 0 && g_lbutton) { int nt = dragTop - (e.y - dragY) / ih; nt = std::max(0, std::min(std::max(0, n - rows), nt)); if (nt != top) { top = nt; draw(); } }
                if (e.type == EV_LUP) {
                    bool moved = dragY >= 0 && abs(e.y - dragY) > 8; dragY = -1;
                    if (bOk.has(e.x, e.y)) act = 1; else if (bCa.has(e.x, e.y)) act = 2;
                    else if (lr.has(e.x, e.y) && !moved) { int i = top + (e.y - lr.t) / ih;
                        if (i < n) { if (i == lastTap && now_ms() - lastT < 600) act = 1; sel = i; lastTap = i; lastT = now_ms(); draw(); } }
                }
                if (e.type == EV_KEY && e.down) {
                    if (e.key == 0x26 && sel > 0) { sel--; if (sel < top) top = sel; draw(); }
                    if (e.key == 0x28 && sel < n - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; draw(); }
                    if (e.key == 0x21) { sel = std::max(0, sel - rows); top = std::max(0, std::min(top, sel)); draw(); }
                    if (e.key == 0x22) { sel = std::min(n - 1, sel + rows); if (sel >= top + rows) top = sel - rows + 1; draw(); }
                    if (e.key == 0x0D) act = 1; if (e.key == 0x1B) act = 2;
                }
            }
            plat_present(); plat_sleep_ms(10);
        }
        screen_restore(sv); plat_present(true);
        if (act == 2) return "";
        if (n == 0 && !save) return "";
        Ent& en = ents[sel];
        if (en.dir) {
            if (en.name == "..") { size_t k = dirw.find_last_of('\\'); dirw = k <= 2 ? dirw.substr(0, 3) : dirw.substr(0, k); }
            else { if (dirw.back() != '\\') dirw += "\\"; dirw += en.name; }
            continue;
        }
        std::string name = en.name;
        if (save && sel == 0) {
            char out[260];
            if (!plat_ime(TR("Dateiname", "File name"), defname.c_str(), out, 64) || !out[0]) continue;
            name = out;
            if (name.find('.') == std::string::npos && defext && *defext) name += std::string(".") + defext;
        }
        std::string full = dirw; if (full.back() != '\\') full += "\\"; full += name;
        return full;
    }
}

static uint32_t ofn_dialog(uint32_t ofn, bool save) {
    uint32_t filt = rd32(ofn + 12), idx = rd32(ofn + 24), file = rd32(ofn + 28), maxf = rd32(ofn + 32);
    uint32_t ftitle = rd32(ofn + 36), maxt = rd32(ofn + 40), idir = rd32(ofn + 44), title = rd32(ofn + 48), defext = rd32(ofn + 60);
    std::string pats = "*.*";
    if (filt) { std::vector<std::string> parts; const char* p = GS(filt); while (*p) { parts.push_back(p); p += strlen(p) + 1; }
        int k = (idx ? idx : 1) - 1; if (2 * k + 1 < (int)parts.size()) pats = parts[2 * k + 1]; else if (parts.size() > 1) pats = parts[1]; }
    std::string dir = g_cwd_win, defname;
    if (file && *GS(file)) { std::string f = GS(file); for (auto& ch : f) if (ch == '/') ch = '\\';
        size_t k = f.find_last_of('\\'); if (k != std::string::npos) { dir = f.substr(0, k); if (dir.size() == 2) dir += "\\"; defname = f.substr(k + 1); } else defname = f;
        if (defname.find('*') != std::string::npos) { if (pats == "*.*") pats = defname; defname.clear(); } }
    if (idir && *GS(idir)) dir = GS(idir);
    if (dir.size() < 2 || dir[1] != ':') { std::string d = g_cwd_win; if (d.back() != '\\') d += "\\"; dir = d + dir; }
    while (dir.size() > 3 && dir.back() == '\\') dir.pop_back();
    const char* t = title ? GS(title) : (save ? "Speichern unter" : "Oeffnen");
    rlog("Dateidialog %s: dir=%s muster=%s", save ? "Speichern" : "Oeffnen", dir.c_str(), pats.c_str());
    std::string res = file_picker(t, dir, pats, save, defname, defext ? GS(defext) : nullptr);
    if (res.empty()) return 0;
    if (res.size() + 1 > maxf) return 0;
    strcpy(GS(file), res.c_str());
    size_t k = res.find_last_of('\\'), e = res.find_last_of('.');
    wr16(ofn + 56, (uint16_t)(k + 1)); wr16(ofn + 58, (uint16_t)(e != std::string::npos && e > k ? e + 1 : res.size()));
    if (ftitle && maxt) { strncpy(GS(ftitle), res.substr(k + 1).c_str(), maxt); GS(ftitle)[maxt - 1] = 0; }
    rlog("Dateidialog -> %s", res.c_str());
    return 1;
}

void install_advapi32() {}
void install_misc() {
    const char* CD = "comdlg32.dll";
    reg(CD, "GetOpenFileNameA", 1, [](Cpu& c) { return ofn_dialog(A(0), false); });
    reg(CD, "GetSaveFileNameA", 1, [](Cpu& c) { return ofn_dialog(A(0), true); });
    reg(CD, "CommDlgExtendedError", 0, [](Cpu&) { return 0u; });

    const char* AV = "AVIFIL32.dll";
    reg(AV, "AVIFileInit", 0, [](Cpu&) { return 0u; });
    reg(AV, "AVIFileExit", 0, [](Cpu&) { return 0u; });
    reg(AV, "AVIFileOpenA", 4, [](Cpu& c) { rlog("AVIFileOpen(%s) -> Video uebersprungen", A(1) ? GS(A(1)) : ""); if (A(0)) wr32(A(0), 0); return 0x80044067u; });
    reg(AV, "AVIFileRelease", 1, [](Cpu&) { return 0u; });
    reg(AV, "AVIFileGetStream", 4, [](Cpu& c) { if (A(1)) wr32(A(1), 0); return 0x80044067u; });
    reg(AV, "AVIStreamRelease", 1, [](Cpu&) { return 0u; });
    reg(AV, "AVIStreamInfoA", 3, [](Cpu& c) { if (A(1)) memset(G(A(1)), 0, A(2)); return 0x80044067u; });
    reg(AV, "AVIStreamReadFormat", 4, [](Cpu&) { return 0x80044067u; });
    reg(AV, "AVIStreamRead", 7, [](Cpu&) { return 0x80044067u; });
    reg(AV, "AVIStreamStart", 1, [](Cpu&) { return 0u; });
    reg(AV, "AVIStreamLength", 1, [](Cpu&) { return 0u; });
    reg(AV, "AVIStreamFindSample", 3, [](Cpu&) { return 0xFFFFFFFFu; });
    reg(AV, "AVIStreamSampleToTime", 2, [](Cpu&) { return 0u; });
    reg(AV, "AVIStreamTimeToSample", 2, [](Cpu&) { return 0u; });
    const char* VF = "MSVFW32.dll";
    reg(VF, "ICLocate", 5, [](Cpu&) { return 0u; });
    reg(VF, "ICClose", 1, [](Cpu&) { return 0u; });
    reg(VF, "ICSendMessage", 4, [](Cpu&) { return 0u; });
    reg(VF, "MCIWndCreateA", 4, [](Cpu& c) { rlog("MCIWndCreate(%s) -> Video uebersprungen", A(3) ? GS(A(3)) : ""); return 0u; });
    reg("DDRAW.dll", "DirectDrawCreate", 3, [](Cpu& c) { rlog("DirectDrawCreate -> nicht verfuegbar (GDI-Modus)"); if (A(1)) wr32(A(1), 0); return 0x80004005u; });
    reg_ord("COMCTL32.dll", 17, 0, [](Cpu&) { return 0u; });
    reg("COMCTL32.dll", "InitCommonControls", 0, [](Cpu&) { return 0u; });

    // XDaemon (Mehrspieler-Netzwerk): abgeschaltet
    const char* XD = "XDaemon.dll";
    static const char* xd[] = { "?XD_SetSecureReceive@@YAXP6AXGPAXKF@Z@Z", "?XD_InFlushSendBuffer@@YAHXZ", "?XD_SetOnClientConnectionToServer@@YAXP6AXF@Z@Z",
        "?XD_ShutdownSockets@@YAXXZ", "?XD_ShutdownModem@@YAXXZ", "?XD_ShutdownTEN@@YAXXZ", "?XD_ResetLibrary@@YAHXZ",
        "?XD_InitializeSocketsTCP@@YAHHHHHIP6AXPAXGJ@Z@Z", "?XD_InitializeSocketsIPXSPX@@YAHHHHHIP6AXPAXGJ@Z@Z", "?XD_InitializeModem@@YAHH@Z",
        "?XD_InitializeSerial@@YAHH@Z", "?XD_ActivateServer@@YAHXZ", "?XD_SetBroadcastReceive@@YAXP6AXPAXGJ@Z@Z", "?XD_LobbySendMessage@@YAHK@Z",
        "?XD_FlushSendBuffer@@YAHK@Z", "?XD_SetNewClientConnection@@YAXP6AXGG@Z@Z", "?XD_SetOnConnectionLost@@YAXP6AXG@Z@Z",
        "?XD_SetOversizedMessageCB@@YAHKP6AXK@Z@Z", "?XD_ServerCloseConnection@@YAHG@Z", "?XD_StopConnections@@YAHXZ", "?XD_SendBroadcastData@@YAHPAXKJ@Z",
        "?XD_SendSecureData@@YAHGPAXKF@Z", "?XD_GetCurrentProtoAddr@@YAPADIH@Z", "?XD_CloseConnection@@YAHXZ", "?XD_OpenConnection@@YAHPAXK@Z",
        "?XD_LaunchedByLobby@@YAHPAXPAULobbyLaunchInfo@@@Z", nullptr };
    for (int i = 0; xd[i]; i++) { std::string n = xd[i]; reg(XD, xd[i], 0, [n](Cpu&) -> uint32_t {
        if (n.find("GetCurrentProtoAddr") != std::string::npos) { static uint32_t s = 0; if (!s) s = gstrdup(""); return s; }
        if (n.find("LaunchedByLobby") != std::string::npos) return 0xFFFFFFFBu;   // -5: "kein Lobby-Spiel" -> normaler Start
        return 0; }, true); }
    reg(XD, "?XD_GetXDaemonVersion@@YAHPAH00@Z", 0, [](Cpu& c) { if (A(0)) wr32(A(0), 1); if (A(1)) wr32(A(1), 0); if (A(2)) wr32(A(2), 0); return 1u; }, true);
}
