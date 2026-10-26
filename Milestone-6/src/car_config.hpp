// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
#ifndef CAR_CONFIG_HPP
#define CAR_CONFIG_HPP

// Vehicle and display profile, read once at start-up.
//
// Source order (first hit wins, per value):
//   1. environment:  NBT_SCREEN=<w>x<h>   NBT_VEHICLE=toyota|bmw
//   2. the car's own CarPlay config, the file Connectivity writes from the vehicle coding
//      (/var/opt/conn/carplay/carplay.ini, header "#Coded is: def,1280_480,touch_screen"):
//      [Screen] Width / Height and [General] Manufacturer / Model
//   3. defaults: 1280x480, Toyota (GR Supra J29)
// NBT_CARPLAY_INI=<path> points at a different file. The factory TEMPLATE
// /opt/conn/data/carplay/carplay.ini is never used: it lists every screen option (900x540 first).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace nbt {

struct CarConfig {
    int screenW, screenH;
    bool bmw;
    std::string iniPath;        // empty = no carplay.ini found
    std::string manufacturer;   // [General] Manufacturer
    std::string model;          // [General] Model, e.g. J29-NBTEvo-0319
    std::string coded;          // "#Coded is: ..." line
    const char* screenFrom;     // "NBT_SCREEN", "carplay.ini", "default"
    const char* vehicleFrom;    // "NBT_VEHICLE", "carplay.ini", "default"
};

namespace carcfg_detail {

inline std::string trim(const char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    std::string r(s);
    while (!r.empty() && (r[r.size() - 1] == '\r' || r[r.size() - 1] == '\n' || r[r.size() - 1] == ' ' ||
                          r[r.size() - 1] == '\t'))
        r.erase(r.size() - 1);
    return r;
}

inline bool ieq(const std::string& a, const char* b) {
    size_t n = strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

inline bool sane(int w, int h) { return w >= 320 && w <= 4096 && h >= 200 && h <= 2160; }

// Parses one carplay.ini. Only the FIRST [Screen] section counts. Returns false if the file can't be read.
inline bool parseIni(const char* path, CarConfig& c, int& w, int& h) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    std::string section;
    int screens = 0;
    w = h = 0;
    while (fgets(line, sizeof(line), f)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#') {
            if (t.compare(0, 10, "#Coded is:") == 0) c.coded = trim(t.c_str() + 10);
            continue;
        }
        if (t[0] == '[') {
            section = t;
            if (section == "[Screen]") ++screens;
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(t.substr(0, eq).c_str()), v = trim(t.substr(eq + 1).c_str());
        if (section == "[General]") {
            if (k == "Manufacturer" && c.manufacturer.empty()) c.manufacturer = v;
            else if (k == "Model" && c.model.empty()) c.model = v;
        } else if (section == "[Screen]" && screens == 1) {
            if (k == "Width") w = atoi(v.c_str());
            else if (k == "Height") h = atoi(v.c_str());
        }
    }
    fclose(f);
    return true;
}

inline CarConfig load() {
    CarConfig c;
    c.screenW = 1280; c.screenH = 480; c.bmw = false;
    c.screenFrom = "default"; c.vehicleFrom = "default";

    int iw = 0, ih = 0;
    const char* forced = getenv("NBT_CARPLAY_INI");
    const char* paths[3] = { forced && *forced ? forced : NULL,
                             "/var/opt/conn/carplay/carplay.ini",
                             "/fs/sda1/opt/conn/carplay/carplay.ini" };
    for (int i = 0; i < 3; ++i) {
        if (!paths[i]) continue;
        CarConfig t = c;
        int w = 0, h = 0;
        if (parseIni(paths[i], t, w, h)) { c = t; c.iniPath = paths[i]; iw = w; ih = h; break; }
    }
    if (sane(iw, ih)) { c.screenW = iw; c.screenH = ih; c.screenFrom = "carplay.ini"; }
    if (!c.manufacturer.empty()) {
        c.bmw = ieq(c.manufacturer, "BMW");
        c.vehicleFrom = "carplay.ini";
    }

    const char* s = getenv("NBT_SCREEN");
    if (s && *s) {
        int w = 0, h = 0;
        if (sscanf(s, "%dx%d", &w, &h) == 2 && sane(w, h)) { c.screenW = w; c.screenH = h; c.screenFrom = "NBT_SCREEN"; }
        else printf("[Car] NBT_SCREEN='%s' ignored (expected e.g. 1440x540)\n", s);
    }
    const char* v = getenv("NBT_VEHICLE");
    if (v && *v) {
        std::string vs(v);
        if (ieq(vs, "bmw")) { c.bmw = true; c.vehicleFrom = "NBT_VEHICLE"; }
        else if (ieq(vs, "toyota")) { c.bmw = false; c.vehicleFrom = "NBT_VEHICLE"; }
        else printf("[Car] NBT_VEHICLE='%s' ignored (toyota or bmw)\n", v);
    }
    return c;
}

} // namespace carcfg_detail

// Parsed on first use. Call it once from main() before any thread starts.
inline const CarConfig& carConfig() {
    static CarConfig c = carcfg_detail::load();
    return c;
}

// The phone always renders a 1280x720 canvas (crop mode). The panel shows it scaled to the panel width,
// so these are the canvas rows the panel can display and the matching MM-Renderer destination height.
inline int canvasRowsShown() {
    const CarConfig& c = carConfig();
    int rows = c.screenH * 1280 / c.screenW;
    return rows > 720 ? 720 : rows;
}
inline int mmrDstHeight() { return 720 * carConfig().screenW / 1280; }

} // namespace nbt

#endif // CAR_CONFIG_HPP
