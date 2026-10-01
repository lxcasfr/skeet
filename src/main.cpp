#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef RGB
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

static ImU32 RGB(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
static ImU32 Hex(unsigned v, int a = 255) { return IM_COL32((v >> 16) & 255, (v >> 8) & 255, v & 255, a); }

static const ImU32 cSidebar = Hex(0x0B0B0B), cContent = Hex(0x101010), cGroup = Hex(0x171717);
static const ImU32 cGroupEdge = Hex(0x282828), cBlack = Hex(0x0A0A0A);
static const ImU32 cText = RGB(204, 204, 204), cTitle = RGB(204, 204, 204), cCombo = RGB(152, 152, 152), cDim = Hex(0x6E6E6E);
static float accent[4] = {149 / 255.f, 212 / 255.f, 84 / 255.f, 1.f};
static ImU32 Accent(float k = 1.f, int a = 255) {
    return IM_COL32((int)(accent[0] * 255 * k), (int)(accent[1] * 255 * k), (int)(accent[2] * 255 * k), a);
}

static float S = 1.f;
static const ImVec2 kOrigin(1.f, -2.f);
static const float kW = 745.f, kH = 673.f;

static const float kGroupW = 299.f, kGroupH = kH - 28.f - 102.f, kRightX = 97.f + kGroupW + 21.f;
static ImDrawList* D = nullptr;

static ImVec2 P(float x, float y) { return ImVec2(std::round((x - kOrigin.x) * S), std::round((y - kOrigin.y) * S)); }
static ImVec2 Mouse() {
    ImVec2 m = ImGui::GetIO().MousePos;
    return ImVec2(m.x / S + kOrigin.x, m.y / S + kOrigin.y);
}
static bool In(float x, float y, float w, float h) {
    ImVec2 m = Mouse();
    return m.x >= x && m.y >= y && m.x < x + w && m.y < y + h;
}
static void Fill(float x, float y, float w, float h, ImU32 c) { D->AddRectFilled(P(x, y), P(x + w, y + h), c); }
static void VGrad(float x, float y, float w, float h, ImU32 top, ImU32 bot) {
    D->AddRectFilledMultiColor(P(x, y), P(x + w, y + h), top, top, bot, bot);
}
static void HGrad(float x, float y, float w, float h, ImU32 l, ImU32 r) {
    D->AddRectFilledMultiColor(P(x, y), P(x + w, y + h), l, r, r, l);
}
static void Outline(float x, float y, float w, float h, ImU32 c) {
    Fill(x, y, w, 1, c);
    Fill(x, y + h - 1, w, 1, c);
    Fill(x, y, 1, h, c);
    Fill(x + w - 1, y, 1, h, c);
}

struct GdiGlyph { float u0, v0, u1, v1; int adv; };
static const wchar_t kExtraGlyphs[] = {0x221E};
static const int kGlyphCount = 224 + (int)(sizeof(kExtraGlyphs) / sizeof(kExtraGlyphs[0]));
struct GdiFont { GLuint tex = 0; int cellW = 0, cellH = 0, pad = 0, ascent = 0, capH = 0; float k = 1; GdiGlyph g[240] = {}; };
static GdiFont g_font[2];

static void BuildGdiFont(GdiFont& F, int cellPx, bool bold, float drawScale) {
    F.k = drawScale;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT hf = CreateFontW(cellPx, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Verdana");
    HGDIOBJ oldFont = SelectObject(dc, hf);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    F.ascent = tm.tmAscent;
    F.capH = (int)std::lround((tm.tmHeight - tm.tmInternalLeading) * 0.727);
    F.pad = 2 + cellPx / 4;
    F.cellW = tm.tmMaxCharWidth + 2 * F.pad;
    F.cellH = tm.tmHeight + 2;
    const int cols = 16, rows = (kGlyphCount + 15) / 16;
    int W = cols * F.cellW, H = rows * F.cellH;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)W * H * 4);
    SetTextColor(dc, 0x00FFFFFF);
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < kGlyphCount; i++) {
        int cx = (i % cols) * F.cellW, cy = (i / cols) * F.cellH;
        wchar_t wc = i < 224 ? (wchar_t)(i + 32) : kExtraGlyphs[i - 224];
        ABC abc;
        if (GetCharABCWidthsW(dc, wc, wc, &abc)) F.g[i].adv = abc.abcA + (int)abc.abcB + abc.abcC;
        else { SIZE sz; GetTextExtentPoint32W(dc, &wc, 1, &sz); F.g[i].adv = sz.cx; }
        TextOutW(dc, cx + F.pad, cy, &wc, 1);
        F.g[i].u0 = (float)cx / W;
        F.g[i].v0 = (float)cy / H;
        F.g[i].u1 = (float)(cx + F.cellW) / W;
        F.g[i].v1 = (float)(cy + F.cellH) / H;
    }
    GdiFlush();
    std::vector<unsigned char> rgba((size_t)W * H * 4);
    const unsigned char* src = (const unsigned char*)bits;
    for (size_t k = 0; k < (size_t)W * H; k++) {
        rgba[k * 4 + 0] = rgba[k * 4 + 1] = rgba[k * 4 + 2] = 255;
        rgba[k * 4 + 3] = (unsigned char)((src[k * 4 + 0] + src[k * 4 + 1] + src[k * 4 + 2] + 1) / 3);
    }
    if (getenv("SKEET_DUMP_FONT")) {
        FILE* f = fopen(bold ? "font_bold.pgm" : "font_reg.pgm", "wb");
        if (f) { fprintf(f, "P5\n%d %d\n255\n", W, H); for (size_t k = 0; k < (size_t)W * H; k++) fputc(rgba[k * 4 + 3], f); fclose(f); }
    }
    glGenTextures(1, &F.tex);
    glBindTexture(GL_TEXTURE_2D, F.tex);
    GLint filter = std::fabs(F.k - 1.f) < 0.01f ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(bmp);
    DeleteObject(hf);
    DeleteDC(dc);
}

static int NextGlyph(const unsigned char*& p) {
    unsigned cp = *p++;
    if (cp >= 0xC0) {
        int extra = cp >= 0xF0 ? 3 : (cp >= 0xE0 ? 2 : 1);
        cp &= (0x3F >> extra);
        for (int k = 0; k < extra && (*p & 0xC0) == 0x80; k++) cp = (cp << 6) | (*p++ & 0x3F);
    }
    if (cp >= 32 && cp < 256) return (int)cp - 32;
    for (int k = 0; k < kGlyphCount - 224; k++)
        if (kExtraGlyphs[k] == (wchar_t)cp) return 224 + k;
    return -1;
}
static float GTextW(int f, const char* s) {
    int w = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p;) {
        int gi = NextGlyph(p);
        if (gi >= 0) w += g_font[f].g[gi].adv;
    }
    return w * g_font[f].k;
}
static void GText(int f, float sx, float sy, ImU32 col, const char* s) {
    const GdiFont& F = g_font[f];
    const float k = F.k;
    float x = std::round(sx), y = std::round(sy);
    for (const unsigned char* p = (const unsigned char*)s; *p;) {
        int gi = NextGlyph(p);
        if (gi < 0) continue;
        const GdiGlyph& g = F.g[gi];
        if (gi != 0)
            D->AddImage((ImTextureID)(intptr_t)F.tex, ImVec2(x - F.pad * k, y), ImVec2(x + (F.cellW - F.pad) * k, y + F.cellH * k),
                        ImVec2(g.u0, g.v0), ImVec2(g.u1, g.v1), col);
        x += g.adv * k;
    }
}
static float TextW(const char* s, bool bold = false) { return GTextW(bold ? 1 : 0, s) / S; }
static ImVec2 TextPos(float x, float cy, int f) {
    ImVec2 p = P(x, cy);
    const GdiFont& F = g_font[f];
    return ImVec2(p.x, std::round(p.y + (F.capH * 0.5f - F.ascent) * F.k));
}
static const ImU32 cShadow = IM_COL32(0, 0, 0, 235);
static void Text(float x, float cy, const char* s, ImU32 col, bool bold = false, bool shadow = true) {
    int f = bold ? 1 : 0;
    ImVec2 p = TextPos(x, cy, f);
    if (shadow && bold) GText(f, p.x + g_font[f].k, p.y + g_font[f].k, cShadow, s);
    GText(f, p.x, p.y, col, s);
}

static void TitleText(float x, float cy, const char* s) {
    Text(x, cy, s, cTitle, true);
}
static void TextOutlined(float x, float cy, const char* s, ImU32 col) {
    ImVec2 p = TextPos(x, cy, 1);
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (dx || dy) GText(1, p.x + dx * g_font[1].k, p.y + dy * g_font[1].k, cShadow, s);
    GText(1, p.x, p.y, col, s);
}

struct Tex { GLuint id = 0; int w = 0, h = 0; };
static std::string g_assets;
static Tex LoadTex(const char* name) {
    Tex t;
    std::string path = g_assets + "/icons/" + name + ".png";
    int n;
    unsigned char* px = stbi_load(path.c_str(), &t.w, &t.h, &n, 4);
    if (!px) return t;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    return t;
}
static Tex g_bgTex;
static void MakeBackgroundTexture() {
    const int N = 4;
    unsigned char px[N * N * 4];
    static const int v[N][N] = {{16, 18, 16, 14}, {14, 16, 18, 16}, {16, 14, 16, 18}, {18, 16, 14, 16}};
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            unsigned char* p = px + (y * N + x) * 4;
            p[0] = p[1] = p[2] = (unsigned char)v[y][x];
            p[3] = 255;
        }
    glGenTextures(1, &g_bgTex.id);
    glBindTexture(GL_TEXTURE_2D, g_bgTex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    g_bgTex.w = g_bgTex.h = N;
}
static void Icon(const Tex& t, float cx, float cy, float size, ImU32 col) {
    if (!t.id) return;
    D->AddImage((ImTextureID)(intptr_t)t.id, P(cx - size / 2, cy - size / 2), P(cx + size / 2, cy + size / 2),
                ImVec2(0, 0), ImVec2(1, 1), col);
}

static bool g_click = false;
static bool g_blocked = false;
static bool g_overWidget = false;
static bool g_clipOn = false;
static float g_clip[4] = {0, 0, 0, 0};
static bool Clicked(float x, float y, float w, float h, bool* hovered = nullptr) {
    bool hov = !g_blocked && In(x, y, w, h) && (!g_clipOn || In(g_clip[0], g_clip[1], g_clip[2], g_clip[3]));
    if (hov) g_overWidget = true;
    if (hovered) *hovered = hov;
    if (hov && g_click) { g_click = false; return true; }
    return false;
}

enum PopupKind { POP_NONE, POP_COMBO, POP_MULTI, POP_COLOR, POP_WEAPON };
struct Popup {
    PopupKind kind = POP_NONE;
    const void* owner = nullptr;
    float x = 0, y = 0, w = 0, h = 0;
    int* index = nullptr;
    bool* flags = nullptr;
    std::vector<std::string> items;
    float scroll = 0;
    float* color = nullptr;
    int drag = 0;
    float hsv[3] = {0, 0, 0};
    const Tex* icons = nullptr;   // weapon list: one icon per row
    bool* enabled = nullptr;      // weapon list: per-weapon checkbox
};
static Popup g_pop;
static const float kItemH = 18.6f;
static const float kComboH = 20.f;
static const int kMaxVisible = 8;

static void ClosePopup() { g_pop = Popup(); }

// Weapon-type list (rage): 40px rows - checkbox at +25, name at +51, icon right-aligned at +w-30 (measured 1:1)
static const float kWeaponRowH = 40.f;
static const int kWeaponVisible = 8;
static void WeaponListRect(float& lx, float& ly, float& lw, float& lh) {
    int n = (int)g_pop.items.size();
    lx = g_pop.x; ly = g_pop.y + g_pop.h + 2; lw = g_pop.w; lh = std::min(n, kWeaponVisible) * kWeaponRowH;
}
static void DrawIconNative(const Tex& t, float rightX, float cy, ImU32 col) {  // icon at its own 1:1 pixel size
    if (!t.id) return;
    float w = (float)t.w, h = (float)t.h;
    float x = std::round(rightX - w), y = std::round(cy - h / 2);
    D->AddImage((ImTextureID)(intptr_t)t.id, P(x, y), P(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), col);
}

static const float kPickW = 176, kPickH = 168;
static void PickerRect(float& px, float& py) {
    px = g_pop.x + g_pop.w - kPickW;
    py = g_pop.y + g_pop.h + 2;
}

static void PopupInput() {
    ImGuiIO& io = ImGui::GetIO();
    if (g_pop.kind == POP_NONE) return;
    if (g_pop.kind == POP_WEAPON) {
        int n = (int)g_pop.items.size();
        float lx, ly, lw, lh;
        WeaponListRect(lx, ly, lw, lh);
        if (In(lx, ly, lw, lh)) {
            g_blocked = true;
            if (io.MouseWheel != 0 && n > kWeaponVisible)
                g_pop.scroll = std::clamp(g_pop.scroll - io.MouseWheel, 0.f, (float)(n - kWeaponVisible));
            if (g_click) {
                g_click = false;
                int i = (int)((Mouse().y - ly) / kWeaponRowH + g_pop.scroll);
                if (i >= 0 && i < n) {
                    if (Mouse().x < lx + 44) g_pop.enabled[i] = !g_pop.enabled[i];  // the checkbox column
                    else { *g_pop.index = i; ClosePopup(); }
                }
            }
        } else if (g_click && !In(g_pop.x, g_pop.y, g_pop.w, g_pop.h)) {
            g_click = false;
            ClosePopup();
        }
        return;
    }
    if (g_pop.kind == POP_COMBO || g_pop.kind == POP_MULTI) {
        int n = (int)g_pop.items.size();
        int vis = std::min(n, kMaxVisible);
        float lx = g_pop.x, ly = g_pop.y + g_pop.h + 1, lw = g_pop.w, lh = vis * kItemH;
        if (In(lx, ly, lw, lh)) {
            g_blocked = true;
            if (io.MouseWheel != 0 && n > kMaxVisible)
                g_pop.scroll = std::clamp(g_pop.scroll - io.MouseWheel, 0.f, (float)(n - kMaxVisible));
            if (g_click) {
                g_click = false;
                int i = (int)((Mouse().y - ly) / kItemH + g_pop.scroll);
                if (i >= 0 && i < n) {
                    if (g_pop.kind == POP_COMBO) { *g_pop.index = i; ClosePopup(); }
                    else g_pop.flags[i] = !g_pop.flags[i];
                }
            }
        } else if (g_click && !In(g_pop.x, g_pop.y, g_pop.w, g_pop.h)) {
            g_click = false;
            ClosePopup();
        }
    } else if (g_pop.kind == POP_COLOR) {
        float px, py;
        PickerRect(px, py);
        bool inside = In(px, py, kPickW, kPickH);
        if (inside) g_blocked = true;
        ImVec2 m = Mouse();
        float svX = px + 5, svY = py + 5, svS = 150, hueX = px + 160, alphaY = py + 159;
        if (g_click && inside) {
            g_click = false;
            if (In(svX, svY, svS, svS)) g_pop.drag = 1;
            else if (In(hueX, svY, 11, svS)) g_pop.drag = 2;
            else if (In(svX, alphaY - 1, svS, 8)) g_pop.drag = 3;
        } else if (g_click && !In(g_pop.x, g_pop.y, g_pop.w, g_pop.h)) {
            g_click = false;
            ClosePopup();
            return;
        }
        if (!io.MouseDown[0]) g_pop.drag = 0;
        if (g_pop.drag) {
            g_blocked = true;
            float* c = g_pop.color;
            float* h = g_pop.hsv;
            if (g_pop.drag == 1) {
                h[1] = std::clamp((m.x - svX) / svS, 0.f, 1.f);
                h[2] = 1.f - std::clamp((m.y - svY) / svS, 0.f, 1.f);
            } else if (g_pop.drag == 2) {
                h[0] = std::clamp((m.y - svY) / svS, 0.f, 0.9999f);
            } else {
                c[3] = std::clamp((m.x - svX) / svS, 0.f, 1.f);
            }
            ImGui::ColorConvertHSVtoRGB(h[0], h[1], h[2], c[0], c[1], c[2]);
        }
    }
}

static void Swatch(float x, float y, const float* c, float w = 16, float h = 9);

static void PopupDraw() {
    if (g_pop.kind == POP_WEAPON) {
        int n = (int)g_pop.items.size();
        float lx, ly, lw, lh;
        WeaponListRect(lx, ly, lw, lh);
        Fill(lx - 1, ly - 1, lw + 2, lh + 2, Hex(0x0C0C0C));
        Fill(lx, ly, lw, lh, Hex(0x232323));
        D->PushClipRect(P(lx, ly), P(lx + lw, ly + lh), true);
        int first = (int)g_pop.scroll;
        for (int k = 0; k < kWeaponVisible + 1 && first + k < n; k++) {
            int i = first + k;
            float ry = ly + k * kWeaponRowH, cy = ry + kWeaponRowH / 2;
            bool hov = In(lx, ry, lw, kWeaponRowH) && In(lx, ly, lw, lh);
            bool sel = *g_pop.index == i;
            if (hov) Fill(lx, ry, lw, kWeaponRowH, Hex(0x181818));
            float bx = lx + 25;
            Fill(bx, cy - 4, 8, 8, cBlack);
            if (g_pop.enabled[i]) Fill(bx + 1, cy - 3, 6, 6, Accent());
            else VGrad(bx + 1, cy - 3, 6, 6, Hex(0x4D4D4D), Hex(0x2F2F2F));
            Text(lx + 51, cy, g_pop.items[i].c_str(), sel ? Accent() : RGB(204, 204, 204), sel);
            DrawIconNative(g_pop.icons[i], lx + lw - 29, cy, sel || hov ? IM_COL32_WHITE : Hex(0x7C7C7C));
        }
        D->PopClipRect();
        if (n > kWeaponVisible) {  // scrollbar
            float th = lh * kWeaponVisible / n, ty = ly + (lh - th) * (g_pop.scroll / (n - kWeaponVisible));
            Fill(lx + lw - 8, ty + 2, 7, th - 4, Hex(0x3A3A3A));
        }
        return;
    }
    if (g_pop.kind == POP_COMBO || g_pop.kind == POP_MULTI) {
        int n = (int)g_pop.items.size();
        int vis = std::min(n, kMaxVisible);
        float lx = g_pop.x, ly = g_pop.y + g_pop.h + 1, lw = g_pop.w, lh = vis * kItemH;
        Fill(lx, ly, lw, lh, Hex(0x232323));
        Outline(lx - 1, ly - 1, lw + 2, lh + 2, Hex(0x0C0C0C));
        D->PushClipRect(P(lx, ly), P(lx + lw, ly + lh), true);
        int first = (int)g_pop.scroll;
        for (int k = 0; k < vis + 1 && first + k < n; k++) {
            int i = first + k;
            float iy = ly + k * kItemH;
            bool hov = In(lx, iy, lw, kItemH) && In(lx, ly, lw, lh);
            if (hov) Fill(lx, iy, lw, kItemH, Hex(0x1A1A1A));
            bool sel = g_pop.kind == POP_COMBO ? *g_pop.index == i : g_pop.flags[i];
            Text(lx + 7, iy + kItemH / 2, g_pop.items[i].c_str(), sel ? Accent() : RGB(205, 205, 205), sel || hov);
        }
        D->PopClipRect();
        if (n > kMaxVisible) {
            float th = lh * vis / n, ty = ly + (lh - th) * (g_pop.scroll / (n - kMaxVisible));
            Fill(lx + lw - 6, ty, 4, th, Hex(0x404040));
        }
    } else if (g_pop.kind == POP_COLOR) {
        float px, py;
        PickerRect(px, py);
        Fill(px, py, kPickW, kPickH, Hex(0x171717));
        Outline(px, py, kPickW, kPickH, Hex(0x0C0C0C));
        Outline(px + 1, py + 1, kPickW - 2, kPickH - 2, cGroupEdge);
        float* h = g_pop.hsv;
        float svX = px + 5, svY = py + 5, svS = 150, hueX = px + 160, alphaY = py + 159;
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h[0], 1, 1, r, g, b);
        ImU32 hue = ImGui::GetColorU32(ImVec4(r, g, b, 1));
        HGrad(svX, svY, svS, svS, IM_COL32_WHITE, hue);
        VGrad(svX, svY, svS, svS, IM_COL32(0, 0, 0, 0), IM_COL32_BLACK);
        Outline(svX - 1, svY - 1, svS + 2, svS + 2, IM_COL32_BLACK);
        ImVec2 cp = P(svX + h[1] * svS, svY + (1 - h[2]) * svS);
        D->AddRect(ImVec2(cp.x - 2, cp.y - 2), ImVec2(cp.x + 3, cp.y + 3), IM_COL32_BLACK);
        D->AddRect(ImVec2(cp.x - 1, cp.y - 1), ImVec2(cp.x + 2, cp.y + 2), IM_COL32_WHITE);
        for (int i = 0; i < 6; i++) {
            float r0, g0, b0, r1, g1, b1;
            ImGui::ColorConvertHSVtoRGB(i / 6.f, 1, 1, r0, g0, b0);
            ImGui::ColorConvertHSVtoRGB((i + 1) / 6.f, 1, 1, r1, g1, b1);
            VGrad(hueX, svY + svS * i / 6, 10, svS / 6 + 0.5f, ImGui::GetColorU32(ImVec4(r0, g0, b0, 1)),
                  ImGui::GetColorU32(ImVec4(r1, g1, b1, 1)));
        }
        Outline(hueX - 1, svY - 1, 12, svS + 2, IM_COL32_BLACK);
        Fill(hueX - 1, svY + h[0] * svS - 1, 12, 3, IM_COL32_WHITE);

        for (int i = 0; i < 38; i++)
            for (int j = 0; j < 2; j++)
                Fill(svX + i * 4, alphaY + j * 3, 4, 3, ((i + j) & 1) ? Hex(0xC8C8C8) : Hex(0xFFFFFF));
        float* c = g_pop.color;
        ImU32 c0 = ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 0)), c1 = ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], 1));
        HGrad(svX, alphaY, svS, 6, c0, c1);
        Outline(svX - 1, alphaY - 1, svS + 2, 8, IM_COL32_BLACK);
        Fill(svX + c[3] * svS - 1, alphaY - 1, 3, 8, IM_COL32_WHITE);
    }
}

struct Group { float x, y, w, h, cy; float* scroll; };
static Group G;
static bool g_groupOpen = false;
static std::unordered_map<std::string, float> g_groupScroll;

static void EndGroup() {
    if (!g_groupOpen) return;
    g_groupOpen = false;
    D->PopClipRect();
    g_clipOn = false;
    float top = G.y + 17, view = G.h - 17 - 4;
    float content = G.cy + *G.scroll - top;
    float maxScroll = content > view + 3 ? content - view + 6 : 0.f;
    ImGuiIO& io = ImGui::GetIO();
    if (maxScroll > 0 && !g_blocked && io.MouseWheel != 0 && In(G.x, G.y, G.w, G.h))
        *G.scroll -= io.MouseWheel * 24.f;
    *G.scroll = std::clamp(*G.scroll, 0.f, maxScroll);
    if (maxScroll > 0) {
        float trackY = G.y + 6, trackH = G.h - 12;
        float th = std::max(18.f, trackH * view / content), ty = trackY + (trackH - th) * (*G.scroll / maxScroll);
        Fill(G.x + G.w - 7, ty, 4, th, Hex(0x404040));
        if (*G.scroll < maxScroll - 1) {
            float ax = G.x + G.w - 14, ay = G.y + G.h - 9;
            D->AddTriangleFilled(P(ax, ay), P(ax + 5, ay), P(ax + 2.5f, ay + 3), Hex(0x979797));
        }
    }
}

static void BeginGroup(const char* title, float x, float y, float w, float h) {
    EndGroup();
    Fill(x - 1, y - 1, w + 2, h + 2, cBlack);
    Fill(x, y, w, h, cGroupEdge);
    Fill(x + 1, y + 1, w - 2, h - 2, cGroup);
    if (title) {
        float tw = TextW(title, true);
        Fill(x + 10, y - 1, tw + 4, 3, cGroup);
        Fill(x + 10, y - 1, tw + 4, 1, cContent);
        TitleText(x + 12, y + 0.5f, title);
    }
    float* sc = &g_groupScroll[std::string(title ? title : "") + "@" + std::to_string((int)x) + "," + std::to_string((int)y)];
    G = {x, y, w, h, y + 17 - *sc, sc};
    g_groupOpen = true;
    D->PushClipRect(P(x + 1, y + 1), P(x + w - 1, y + h - 1), true);
    g_clipOn = true;
    g_clip[0] = x + 1; g_clip[1] = y + 1; g_clip[2] = w - 2; g_clip[3] = h - 2;
}
static float RowX() { return G.x + 37; }
static float ControlW() { return std::min(G.w - 93, 180.f); }

static void Swatch(float x, float y, const float* c, float w, float h) {
    Fill(x, y, w, h, cBlack);
    float ix = x + 1, iy = y + 1, iw = w - 2, ih = h - 2;
    if (c[3] < 0.999f)
        for (int i = 0; i < (int)(iw / 3.5f) + 1; i++)
            for (int j = 0; j < 2; j++)
                Fill(ix + i * 3.5f, iy + j * 3.5f, std::min(3.5f, iw - i * 3.5f), std::min(3.5f, ih - j * 3.5f),
                     ((i + j) & 1) ? Hex(0xB0B0B0) : Hex(0xFFFFFF));
    ImVec4 top(c[0], c[1], c[2], c[3]), bot(c[0] * 0.8f, c[1] * 0.8f, c[2] * 0.8f, c[3]);
    VGrad(ix, iy, iw, ih, ImGui::GetColorU32(top), ImGui::GetColorU32(bot));
}

static void ColorButtons(float cy, std::initializer_list<float*> colors) {
    float x = G.x + G.w - 33;
    std::vector<float*> v(colors);
    for (int k = (int)v.size() - 1; k >= 0; k--) {
        float* c = v[k];
        bool hov;
        if (Clicked(x, cy - 6, 16, 9, &hov)) {
            if (g_pop.kind == POP_COLOR && g_pop.color == c) ClosePopup();
            else {
                ClosePopup();
                g_pop.kind = POP_COLOR;
                g_pop.color = c;
                g_pop.x = x; g_pop.y = cy - 6; g_pop.w = 16; g_pop.h = 9;
                ImGui::ColorConvertRGBtoHSV(c[0], c[1], c[2], g_pop.hsv[0], g_pop.hsv[1], g_pop.hsv[2]);
            }
        }
        Swatch(x, cy - 6, c);
        x -= 18.5f;
    }
}

struct Bind { int key = 0; bool waiting = false; };
static Bind* g_waiting = nullptr;
static std::string KeyName(int k) {
    if (k == 0) return "-";
    if (k == -1) return "M4";
    if (k == -2) return "M5";
    switch (k) {
        case ImGuiKey_LeftCtrl: case ImGuiKey_RightCtrl: return "CTR";
        case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return "SHF";
        case ImGuiKey_LeftAlt: case ImGuiKey_RightAlt: return "ALT";
        case ImGuiKey_Delete: return "DEL";
        case ImGuiKey_Insert: return "INS";
        default: break;
    }
    const char* n = ImGui::GetKeyName((ImGuiKey)k);
    std::string s = n ? n : "?";
    for (auto& ch : s) ch = (char)toupper((unsigned char)ch);
    if (s.size() > 6) s = s.substr(0, 6);
    return s;
}
static void KeyBind(float cy, Bind* b) {
    std::string label = "[" + (b->waiting ? std::string("...") : KeyName(b->key)) + "]";
    float w = TextW(label.c_str());
    float x = G.x + G.w - 17 - w;
    bool hov;
    if (Clicked(x - 2, cy - 6, w + 4, 12, &hov)) {
        b->waiting = true;
        g_waiting = b;
    }
    Text(x, cy, label.c_str(), b->waiting ? Accent() : (hov ? Hex(0x8A8A8A) : cDim), false);
}

static const ImU32 cSpecial = RGB(182, 182, 101);
static const ImU32 cDisabled = RGB(78, 78, 78);
static float Checkbox(const char* label, bool* v, ImU32 labelCol = 0, bool disabled = false) {
    float ry = G.cy, cy = ry + 9;
    float bx = G.x + 17;
    bool hov = false;
    if (!disabled && Clicked(bx - 2, ry, RowX() - bx + 2 + TextW(label) + 4, 18, &hov)) *v = !*v;
    Fill(bx, cy - 4, 8, 8, cBlack);
    if (disabled) VGrad(bx + 1, cy - 3, 6, 6, Hex(0x2A2A2A), Hex(0x222222));
    else if (*v) Fill(bx + 1, cy - 3, 6, 6, Accent());
    else VGrad(bx + 1, cy - 3, 6, 6, hov ? Hex(0x555555) : Hex(0x4D4D4D), hov ? Hex(0x373737) : Hex(0x2F2F2F));
    Text(RowX(), cy, label, disabled ? cDisabled : (labelCol ? labelCol : cText));
    G.cy += 18;
    return cy;
}

static float LabelRow(const char* label) {
    float cy = G.cy + 9;
    Text(RowX(), cy, label, cText);
    G.cy += 18;
    return cy;
}

static void Label(const char* label) {
    Text(RowX(), G.cy + 7.5f, label, cText);
    G.cy += 16;
}

static bool g_comboDisabled = false;
static void ComboBox(const void* owner, const std::string& value, bool open, bool* clicked) {
    float x = RowX(), y = G.cy, w = ControlW(), h = kComboH;
    bool hov = false;
    *clicked = g_comboDisabled ? false : Clicked(x, y, w, h, &hov);
    bool lit = hov || open;
    Fill(x - 1, y, w + 2, h, Hex(0x0F0F0F));
    Fill(x, y + 1, w, 1, lit ? Hex(0x191919) : Hex(0x141414));
    VGrad(x, y + 2, w, h - 3, lit ? Hex(0x262626) : Hex(0x1F1F1F), lit ? Hex(0x2C2C2C) : Hex(0x242424));
    D->PushClipRect(P(x, y), P(x + w - 16, y + h), true);
    Text(x + 7, y + h / 2, value.c_str(), g_comboDisabled ? Hex(0x5C5C5C) : cCombo);
    D->PopClipRect();

    ImVec2 a = P(x + w - 10, y + h / 2 - 1.5f), b = P(x + w - 5, y + h / 2 - 1.5f), c = P(x + w - 7.5f, y + h / 2 + 1.5f);
    D->AddTriangleFilled(a, b, c, open ? Hex(0xC8C8C8) : Hex(0x979797));
    (void)owner;
    G.cy += kComboH + 1;
}

static void Combo(int* index, const std::vector<std::string>& items) {
    bool open = g_pop.kind == POP_COMBO && g_pop.owner == index, clicked;
    float x = RowX(), y = G.cy;
    ComboBox(index, items[*index], open, &clicked);
    if (clicked) {
        if (open) ClosePopup();
        else {
            ClosePopup();
            g_pop.kind = POP_COMBO; g_pop.owner = index; g_pop.index = index; g_pop.items = items;
            g_pop.x = x; g_pop.y = y; g_pop.w = ControlW(); g_pop.h = kComboH;
        }
    }
}

static void MultiCombo(bool* flags, const std::vector<std::string>& items, float startScroll = 0) {
    bool open = g_pop.kind == POP_MULTI && g_pop.owner == flags, clicked;
    std::string value;
    for (size_t i = 0; i < items.size(); i++)
        if (flags[i]) value += (value.empty() ? "" : ", ") + items[i];
    if (value.empty()) value = "-";
    float x = RowX(), y = G.cy;
    ComboBox(flags, value, open, &clicked);
    if (clicked) {
        if (open) ClosePopup();
        else {
            ClosePopup();
            g_pop.kind = POP_MULTI; g_pop.owner = flags; g_pop.flags = flags; g_pop.items = items;
            g_pop.x = x; g_pop.y = y; g_pop.w = ControlW(); g_pop.h = kComboH;
            g_pop.scroll = std::min(startScroll, std::max(0.f, (float)items.size() - kMaxVisible));
        }
    }
}

static float* g_dragSlider = nullptr;

static void Slider(float* v, float mn, float mx, const char* fmt, float step = 1.f, bool inf = false) {
    float x = RowX(), y = G.cy + 2, w = ControlW(), h = 8;
    ImGuiIO& io = ImGui::GetIO();
    bool hov;
    if (Clicked(x, y - 2, w, h + 4, &hov)) g_dragSlider = v;
    if (g_dragSlider == v) {
        if (!io.MouseDown[0]) g_dragSlider = nullptr;
        else *v = std::round((mn + std::clamp((Mouse().x - x - 1) / (w - 2), 0.f, 1.f) * (mx - mn)) / step) * step;
    }

    if (Clicked(x - 9, y - 2, 7, 12)) *v = std::max(mn, *v - step);
    if (Clicked(x + w + 2, y - 2, 7, 12)) *v = std::min(mx, *v + step);
    Text(x - 8, y + 3.5f, "-", Hex(0x464646), true, false);
    Text(x + w + 3, y + 3.5f, "+", Hex(0x464646), true, false);
    Fill(x, y, w, h, Hex(0x0F0F0F));
    VGrad(x + 1, y + 1, w - 2, h - 2, hov ? Hex(0x3A3A3A) : Hex(0x343434), hov ? Hex(0x474747) : Hex(0x414141));
    float t = (*v - mn) / (mx - mn), fw = (w - 2) * t;
    if (fw > 0) VGrad(x + 1, y + 1, fw, h - 2, Accent(1.0f), Accent(0.78f));
    char buf[32];
    if (inf && *v >= mx) snprintf(buf, sizeof(buf), "\xE2\x88\x9E");
    else snprintf(buf, sizeof(buf), fmt, *v);
    float tw = TextW(buf, true);
    TextOutlined(std::clamp(x + 1 + fw - tw / 2, x - 4, x + w - tw + 4), y + 8.5f, buf, IM_COL32(255, 255, 255, 200));
    G.cy += 14;
}

// Button (Load / Save / ...): dark gradient, black outer + lighter inner border, bold centred label.
static bool ButtonAt(const char* label, float x, float y, float w, float h, bool disabled) {
    bool hov = false;
    bool pressed = !disabled && Clicked(x, y, w, h, &hov);
    bool held = hov && ImGui::IsMouseDown(0);
    Fill(x - 1, y - 1, w + 2, h + 2, Hex(0x0C0C0C));
    Fill(x, y, w, h, disabled ? Hex(0x262626) : Hex(0x323232));
    VGrad(x + 1, y + 1, w - 2, h - 2, disabled ? Hex(0x1C1C1C) : (held ? Hex(0x1A1A1A) : (hov ? Hex(0x282828) : Hex(0x222222))),
          disabled ? Hex(0x1A1A1A) : (held ? Hex(0x222222) : (hov ? Hex(0x202020) : Hex(0x1A1A1A))));
    float tw = TextW(label, true);
    Text(x + (w - tw) / 2, y + h / 2, label, disabled ? Hex(0x4E4E4E) : RGB(220, 220, 220), true);
    return pressed;
}
static bool Button(const char* label, bool disabled = false) {
    bool r = ButtonAt(label, RowX(), G.cy, ControlW(), 22, disabled);
    G.cy += 31;
    return r;
}
static bool ButtonOld(const char* label) {
    float x = RowX(), y = G.cy, w = ControlW(), h = 22;
    bool hov;
    bool pressed = Clicked(x, y, w, h, &hov);
    bool held = hov && ImGui::IsMouseDown(0);
    Fill(x - 1, y - 1, w + 2, h + 2, Hex(0x0C0C0C));
    Fill(x, y, w, h, Hex(0x323232));
    VGrad(x + 1, y + 1, w - 2, h - 2, held ? Hex(0x1A1A1A) : (hov ? Hex(0x282828) : Hex(0x222222)),
          held ? Hex(0x222222) : (hov ? Hex(0x202020) : Hex(0x1A1A1A)));
    float tw = TextW(label, true);
    Text(x + (w - tw) / 2, y + h / 2, label, RGB(220, 220, 220), true);
    G.cy += 31;
    return pressed;
}

// Simple list (config presets): selected entry in the accent colour.
static void PresetList(const std::vector<std::string>& items, int* sel, float h) {
    float x = RowX(), y = G.cy, w = ControlW();
    Fill(x - 1, y - 1, w + 2, h + 2, Hex(0x0F0F0F));
    Fill(x, y, w, h, Hex(0x1C1C1C));
    for (size_t i = 0; i < items.size(); i++) {
        float iy = y + 2 + i * 18;
        bool hov;
        if (Clicked(x, iy, w, 18, &hov)) *sel = (int)i;
        if (hov) Fill(x, iy, w, 18, Hex(0x222222));
        bool on = *sel == (int)i;
        Text(x + 8, iy + 9, items[i].c_str(), on ? Accent() : RGB(200, 200, 200), on);
    }
    G.cy += h + 6;
}

// Plain input box at any geometry (placeholder shown dim when empty; disabled = greyed, not editable).
static std::string* g_editing3 = nullptr;
static void InputBoxAt(std::string* text, const char* placeholder, float x, float y, float w, float h, bool disabled) {
    bool hov = false;
    if (!disabled && Clicked(x, y, w, h, &hov)) g_editing3 = text;
    else if (g_click && g_editing3 == text) g_editing3 = nullptr;
    bool focus = g_editing3 == text;
    if (focus) {
        ImGuiIO& io = ImGui::GetIO();
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 32 && c < 127 && text->size() < 32) text->push_back((char)c);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !text->empty()) text->pop_back();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) g_editing3 = nullptr;
    }
    Fill(x - 1, y, w + 2, h, Hex(0x0F0F0F));
    Fill(x, y + 1, w, h - 2, disabled ? Hex(0x171717) : (focus ? Hex(0x1E1E1E) : Hex(0x1A1A1A)));
    if (text->empty() && !focus) Text(x + 6, y + h / 2, placeholder, disabled ? Hex(0x3C3C3C) : Hex(0x5A5A5A));
    else {
        std::string shown = *text + (focus && fmodf((float)ImGui::GetTime(), 1.f) < 0.55f ? "_" : "");
        Text(x + 6, y + h / 2, shown.c_str(), disabled ? Hex(0x4E4E4E) : RGB(200, 200, 200));
    }
}

// Text field with an "x" button that clears it (config name).
static std::string* g_editing2 = nullptr;
static void NameField(std::string* text) {
    float x = RowX(), y = G.cy, w = ControlW(), h = 18;
    bool hov, xhov;
    bool clear = Clicked(x + w - 16, y, 16, h, &xhov);
    if (clear) text->clear();
    else if (Clicked(x, y, w - 16, h, &hov)) g_editing2 = text;
    else if (g_click && g_editing2 == text) g_editing2 = nullptr;
    ImGuiIO& io = ImGui::GetIO();
    bool focus = g_editing2 == text;
    if (focus) {
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 32 && c < 127 && text->size() < 24) text->push_back((char)c);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !text->empty()) text->pop_back();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) g_editing2 = nullptr;
    }
    Fill(x - 1, y, w + 2, h, Hex(0x0F0F0F));
    Fill(x, y + 1, w, h - 2, focus ? Hex(0x1E1E1E) : Hex(0x1A1A1A));
    std::string shown = *text + (focus && fmodf((float)ImGui::GetTime(), 1.f) < 0.55f ? "_" : "");
    Text(x + 6, y + h / 2, shown.c_str(), RGB(200, 200, 200));
    ImVec2 c = P(x + w - 8, y + h / 2);  // the clear cross
    float r = 2.5f * S;
    ImU32 xc = xhov ? Hex(0xDADADA) : Hex(0x8A8A8A);
    D->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r + 1, c.y + r + 1), xc, 1.f);
    D->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r + 1, c.y - r - 1), xc, 1.f);
    G.cy += h + 8;
}

static void IconTabs(const Tex* icons, int n, int* sel, float iconH) {
    (void)iconH;
    float cy = G.y + 39;
    for (int i = 0; i < n; i++) {
        float cx = G.x + 62 + i * 101.f;
        float iw = (float)icons[i].w;
        iconH = (float)icons[i].h;
        bool hov;
        if (Clicked(cx - iw / 2 - 4, cy - iconH / 2 - 4, iw + 8, iconH + 8, &hov)) *sel = i;
        if (icons[i].id)
            D->AddImage((ImTextureID)(intptr_t)icons[i].id, P(std::round(cx - iw / 2), std::round(cy - iconH / 2)),
                        P(std::round(cx - iw / 2) + iw, std::round(cy - iconH / 2) + iconH),
                        ImVec2(0, 0), ImVec2(1, 1), *sel == i ? IM_COL32_WHITE : (hov ? Hex(0xA8A8A8) : Hex(0x7A7A7A)));
    }
}

static void IconCombo(float cy, const Tex& icon, int* index, const std::vector<std::string>& items) {
    float h = 16, iw = icon.h ? h * icon.w / icon.h : h, w = iw + 14;
    float x = G.x + G.w - 17 - w, y = cy - h / 2;
    bool open = g_pop.kind == POP_COMBO && g_pop.owner == index, hov;
    if (Clicked(x, y, w, h, &hov)) {
        if (open) ClosePopup();
        else {
            ClosePopup();
            float lw = 120;
            g_pop.kind = POP_COMBO; g_pop.owner = index; g_pop.index = index; g_pop.items = items;
            g_pop.x = x + w - lw; g_pop.y = y; g_pop.w = lw; g_pop.h = h;
        }
    }
    if (icon.id)
        D->AddImage((ImTextureID)(intptr_t)icon.id, P(x, y), P(x + iw, y + h), ImVec2(0, 0), ImVec2(1, 1),
                    hov || open ? IM_COL32_WHITE : Hex(0xC8C8C8));
    float ax = x + w - 7, ay = cy - 1;
    D->AddTriangleFilled(P(ax - 2.5f, ay), P(ax + 2.5f, ay), P(ax, ay + 3), open ? Hex(0xC8C8C8) : Hex(0x979797));
}

static std::string* g_editing = nullptr;
static void TextField(std::string* text) {
    float x = RowX(), y = G.cy, w = ControlW(), h = 18;
    bool hov;
    if (Clicked(x, y, w, h, &hov)) g_editing = text;
    else if (g_click && g_editing == text) g_editing = nullptr;
    ImGuiIO& io = ImGui::GetIO();
    bool focus = g_editing == text;
    if (focus) {
        for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 32 && c < 127 && text->size() < 32) text->push_back((char)c);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !text->empty()) text->pop_back();
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) g_editing = nullptr;
    }
    Fill(x - 1, y, w + 2, h, Hex(0x0F0F0F));
    Fill(x, y + 1, w, h - 2, focus || hov ? Hex(0x1E1E1E) : Hex(0x1A1A1A));
    std::string shown = *text + (focus && fmodf((float)ImGui::GetTime(), 1.f) < 0.55f ? "_" : (focus ? "" : "_"));
    Text(x + 6, y + h / 2, shown.c_str(), RGB(196, 202, 150));
    G.cy += h + 1;
}

static void ListBox(const std::vector<std::pair<std::string, ImU32>>& items, int* sel, float* scroll) {
    float x = RowX(), y = G.cy, w = ControlW(), h = G.y + G.h - 14 - y;
    if (h < 40) h = 40;
    const float ih = 17.f;
    Fill(x - 1, y, w + 2, h, Hex(0x0F0F0F));
    Fill(x, y, w, h - 1, Hex(0x1C1C1C));
    float content = items.size() * ih + 4, maxS = std::max(0.f, content - h);
    ImGuiIO& io = ImGui::GetIO();
    if (maxS > 0 && io.MouseWheel != 0 && In(x, y, w, h) && !g_blocked) { *scroll -= io.MouseWheel * ih * 2; io.MouseWheel = 0; }
    *scroll = std::clamp(*scroll, 0.f, maxS);
    D->PushClipRect(P(x, y), P(x + w, y + h - 1), true);
    float sclip[4] = {g_clip[0], g_clip[1], g_clip[2], g_clip[3]};
    for (size_t i = 0; i < items.size(); i++) {
        float iy = y + 2 + i * ih - *scroll;
        if (iy + ih < y || iy > y + h) continue;
        bool hov;
        bool inside = In(x, y, w, h);
        if (Clicked(x, iy, w, ih, &hov) && inside) *sel = (int)i;
        hov = hov && inside;
        bool on = *sel == (int)i;
        if (hov) Fill(x, iy, w, ih, Hex(0x222222));
        Fill(x + 7, iy + ih / 2 - 1.5f, 3, 3, items[i].second);
        Text(x + 16, iy + ih / 2, items[i].first.c_str(), on || hov ? RGB(236, 236, 236) : RGB(200, 200, 200), on || hov);
    }
    D->PopClipRect();
    (void)sclip;
    if (maxS > 0) {
        float th = std::max(16.f, h * h / content), ty = y + (h - th) * (*scroll / maxS);
        Fill(x + w - 5, ty, 4, th, Hex(0x404040));
    }
    G.cy += h + 2;
}

struct Col { float v[4]; };
static Col MakeCol(unsigned hex, float a = 1.f) {
    return {{((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a}};
}

struct EspPage {
    bool enabled = true, box = false, health = false, name = true, avatar = true, flags = true;
    Bind enabledKey;
    Col boxCol = MakeCol(0xECECEC), nameCol = MakeCol(0xECECEC);
    Col flagCol[3] = {MakeCol(0xECECEC), MakeCol(0xA6DCE8), MakeCol(0xE0334F)};
    bool flagSel[13] = {true, true, true, true, true, true, true, true, true, true, false, false, false};
    bool rows[7] = {false, true, true, false, false, false, false};
    Col rowCol[7] = {MakeCol(0xEDEFBC), MakeCol(0xECECEC), MakeCol(0x5B8FD6), MakeCol(0xECECEC), MakeCol(0xECECEC),
                     MakeCol(0x4A5DE0), MakeCol(0x9B59E8)};
    bool weaponSel[4] = {true, true, true, false};

    bool player = true, walls = true, glow = true, shadow = false, onshot = false;
    int playerMat = 4, wallsMat = 1, ragdoll = 0;
    float glowAmt = 166;
    Col playerCol = MakeCol(0xC5E86F), wallsCol = MakeCol(0x5B8FD6), glowCol = MakeCol(0xD89AAE, 0.55f),
        shadowCol = MakeCol(0xFFFFFF, 0.6f), onshotCol = MakeCol(0xE6E6E6, 0.5f);
};
static EspPage g_esp[2];
static int g_tab = 2, g_category = 0;
static const std::vector<std::string> kFlags = {"Money", "Kevlar / Helmet", "Scoped", "Flashed", "Reloading",
                                                "Vulnerable", "Slowed down", "Defusing", "Bomb / Hostage", "Pin pulled",
                                                "Spawn protection", "Defuse kit", "Ping"};
static const std::vector<std::string> kMaterials = {"Default", "Solid", "Shaded", "Metallic", "Glow", "Glass"};
static const std::vector<std::string> kRagdolls = {"-", "Push", "Float", "Zero gravity"};
static const char* kRowNames[7] = {"Skeleton", "Weapon", "Ammo", "Distance", "Off-screen arrow", "Visualize aimbot",
                                   "Bullet tracers"};
static const std::vector<std::string> kWeaponEsp = {"Icon", "Text", "Show all grenades", "Distance"};

static void PageEsp(EspPage& e, bool team) {
    const float L = 97, R = kRightX, T = 102, W = kGroupW, H = kGroupH;
    BeginGroup(team ? "Teammate ESP" : "Enemy ESP", L, T, W, H);
    float cy = Checkbox("Enabled", &e.enabled);
    KeyBind(cy, &e.enabledKey);
    cy = Checkbox("Bounding box", &e.box);
    ColorButtons(cy, {e.boxCol.v});
    Checkbox("Health bar", &e.health);
    cy = Checkbox("Name", &e.name);
    ColorButtons(cy, {e.nameCol.v});
    Checkbox("Avatar", &e.avatar);
    cy = Checkbox("Flags", &e.flags);
    ColorButtons(cy, {e.flagCol[0].v, e.flagCol[1].v, e.flagCol[2].v});
    MultiCombo(e.flagSel, kFlags, 5);
    for (int i = 0; i < 7; i++) {
        cy = Checkbox(kRowNames[i], &e.rows[i]);
        ColorButtons(cy, {e.rowCol[i].v});
        if (i == 1) MultiCombo(e.weaponSel, kWeaponEsp);
    }

    BeginGroup(team ? "Teammate models" : "Enemy models", R, T, W, H);
    cy = Checkbox("Player", &e.player);
    ColorButtons(cy, {e.playerCol.v});
    Combo(&e.playerMat, kMaterials);
    Slider(&e.glowAmt, 0, 600, "%.0f%%");
    cy = Checkbox("Player through walls", &e.walls);
    ColorButtons(cy, {e.wallsCol.v});
    Combo(&e.wallsMat, kMaterials);
    cy = Checkbox("Glow", &e.glow);
    ColorButtons(cy, {e.glowCol.v});
    cy = Checkbox("Shadow", &e.shadow);
    ColorButtons(cy, {e.shadowCol.v});
    cy = Checkbox("On shot", &e.onshot);
    ColorButtons(cy, {e.onshotCol.v});
    Label("Ragdolls");
    Combo(&e.ragdoll, kRagdolls);
}

struct Opt { const char* label; int kind; };
struct SimplePage {
    const char* left;
    const char* right;
    std::vector<Opt> a, b;
    std::vector<bool> av, bv;
    std::vector<float> as, bs;
    std::vector<int> ac, bc;
};
static const std::vector<std::string> kGeneric = {"Default", "Aggressive", "Passive", "Custom"};
static void Column(const char* title, float x, std::vector<Opt>& opts, std::vector<bool>& vals, std::vector<float>& sl,
                   std::vector<int>& cb) {
    BeginGroup(title, x, 102, kGroupW, kGroupH);
    if (vals.size() != opts.size()) { vals.assign(opts.size(), false); sl.assign(opts.size(), 50); cb.assign(opts.size(), 0); }
    for (size_t i = 0; i < opts.size(); i++) {
        switch (opts[i].kind) {
            case 0: { bool b = vals[i]; Checkbox(opts[i].label, &b); vals[i] = b; break; }
            case 1: Label(opts[i].label); Slider(&sl[i], 0, 100, "%.0f"); break;
            case 2: Label(opts[i].label); Combo(&cb[i], kGeneric); break;
            default: Label(opts[i].label);
        }
    }
}
static SimplePage g_pages[7] = {
    {"Aimbot", "Other",
     {{"Enabled", 0}, {"Target selection", 2}, {"Automatic fire", 0}, {"Automatic penetration", 0}, {"Silent aim", 0},
      {"Minimum hit chance", 1}, {"Minimum damage", 1}, {"Automatic scope", 0}, {"Reduce aim step", 0},
      {"Maximum FOV", 1}},
     {{"Remove recoil", 0}, {"Accuracy boost", 2}, {"Delay shot", 0}, {"Quick stop", 0}, {"Quick peek assist", 0},
      {"Anti-aim correction", 0}, {"Prefer body aim", 0}, {"Force body aim on peek", 0}, {"Duck peek assist", 0}}},
    {"Weapon type", "Triggerbot",
     {{"Enabled", 0}, {"Speed", 1}, {"Speed (in attack)", 1}, {"Speed scale - FOV", 1}, {"Maximum lock-on time", 1},
      {"Reaction time", 1}, {"Maximum FOV", 1}, {"Recoil compensation", 1}, {"Quick stop", 0}},
     {{"Enabled", 0}, {"Minimum hit chance", 1}, {"Reaction time", 1}, {"Burst fire", 0}, {"Automatic penetration", 0}}},
    {}, {},
    {"Knife options", "Glove options",
     {{"Override knife", 0}, {"Knife model", 2}, {"StatTrak", 0}, {"Wear", 1}, {"Seed", 1}},
     {{"Override gloves", 0}, {"Glove model", 2}, {"Wear", 1}}},
    {"Players", "Adjustments",
     {{"Show all players", 0}, {"Sort by", 2}, {"Hide bots", 0}},
     {{"Add to whitelist", 0}, {"Allow shared ESP updates", 0}, {"Force body yaw", 0}, {"Correction active", 0}}},
    {"Presets", "Lua",
     {{"Load on startup", 0}, {"Preset", 2}, {"Auto save", 0}},
     {{"Allow unsafe scripts", 0}, {"Script", 2}, {"Load on startup", 0}}},
};
static SimplePage g_misc = {"Miscellaneous", "Movement",
                            {{"Override FOV", 1}, {"Override zoom FOV", 1}, {"Knifebot", 0}, {"Zeusbot", 0},
                             {"Automatic weapons", 0}, {"Reveal competitive ranks", 0}, {"Clan tag spammer", 0},
                             {"Log weapon purchases", 0}, {"Log damage dealt", 0}, {"Persistent kill feed", 0}},
                            {{"Bunny hop", 0}, {"Air strafe", 0}, {"Air strafe direction", 2}, {"Infinite duck", 0},
                             {"Easy strafe", 0}, {"Fast walk", 0}}};
static SimplePage g_visOther[3] = {
    {"Other ESP", "Effects",
     {{"Radar", 0}, {"Dropped weapons", 0}, {"Grenades", 0}, {"Inaccuracy overlay", 0}, {"Recoil overlay", 0},
      {"Crosshair", 0}, {"Bomb", 0}, {"Grenade trajectory", 0}, {"Spectators", 0}},
     {{"Remove flashbang effects", 0}, {"Remove smoke grenades", 0}, {"Remove fog", 0}, {"Remove skybox", 0},
      {"Visual recoil adjustment", 2}, {"Transparent walls", 1}, {"Transparent props", 1}, {"Brightness adjustment", 2},
      {"Remove scope overlay", 0}, {"Disable post processing", 0}}},
    {"Colored models", "Other",
     {{"Hands", 0}, {"Weapon viewmodel", 0}, {"Weapons", 0}, {"Disable model occlusion", 0}, {"Shadow", 0}},
     {{"Force third person (alive)", 0}, {"Force third person (dead)", 0}, {"Thirdperson distance", 1},
      {"Bullet tracers", 0}, {"Bullet impacts", 0}}},
};

static Tex g_weapons[6], g_weaponGlobal;
static Tex g_weaponList[11];  // icons of the weapon-type list
static void WeaponHeader(int* index, bool* enabled, const std::vector<std::string>& items) {
    bool open = g_pop.kind == POP_WEAPON && g_pop.owner == index, hov;
    if (Clicked(G.x, G.y + 4, G.w, G.h - 8, &hov)) {
        if (open) ClosePopup();
        else {
            ClosePopup();
            g_pop.kind = POP_WEAPON; g_pop.owner = index; g_pop.index = index; g_pop.items = items;
            g_pop.enabled = enabled; g_pop.icons = g_weaponList;
            g_pop.x = G.x; g_pop.y = G.y; g_pop.w = G.w; g_pop.h = G.h;
            g_pop.scroll = std::clamp((float)*index - 3, 0.f, std::max(0.f, (float)items.size() - kWeaponVisible));
        }
    }
    float cy = G.y + 30.5f;
    Text(G.x + 52, cy, items[*index].c_str(), RGB(210, 210, 210), true);
    DrawIconNative(g_weaponList[*index], G.x + G.w - 29, G.y + 30, IM_COL32_WHITE);
    float ax = G.x + G.w - 12.5f, ay = G.y + 28;
    D->AddTriangleFilled(P(ax - 2.5f, ay), P(ax + 2.5f, ay), P(ax, ay + 3), open || hov ? Hex(0xC8C8C8) : Hex(0x979797));
}
static const float kTop = 27.f, kBottom = kH - 28.f;

struct RagePage {
    bool global = true, enabled = false, mhcOverride = false, mdOverride = false, preferBody = false, quickStop = false,
         autoScope = false;
    Bind enabledKey{-2}, mhcKey, mdKey, forceBodyKey, quickStopKey;
    int weapon = 5, multiPoint = 0, accuracy = 1;
    bool weaponOn[11] = {true, true, true, true, true, true, true, true, true, true, true};
    bool hitbox[7] = {true, false, false, false, false, false, false};
    float multiScale = 90, hitChance = 50, minDamage = 10, maxFov = 180;
    bool autoFire = false, throughWalls = false, silent = false, removeRecoil = false, removeSpread = false,
         rapidFire = false, quickPeek = false, duckPeek = false, limitStep = true;
    Bind rapidKey, peekKey, duckKey;

    bool aaEnabled = true, aaJitter = false, aaSpin = false, freestanding = true, edgeYaw = false;
    int pitch = 1, yawBase = 0, bodyYaw = 0;
    float yawAdd = 0, fsOffset = 0, bodyYawAmt = 0, fakeLimit = 60;
    Bind aaKey, jitterKey{ImGuiKey_H}, spinKey, fsKey{ImGuiKey_X};
};
struct LegitPage {
    int weapon = 2;
    bool enabled = false, quickStop = false, smoke = false, blind = false;
    Bind key{-2}, trigKey{-2};
    bool hitbox[5] = {true, false, false, false, false}, trigHitbox[5] = {true, false, false, false, false};
    float fov = 10, speed = 0.65f, speedAttack = 0.65f, speedScale = 100, reaction = 100, lockOn = 1000, rcsP = 75,
          rcsY = 0;
    bool trigEnabled = false, burst = false, trigWalls = false, trigSmoke = false, trigBlind = false, standalone = false;
    float trigHitChance = 75, trigDamage = 1, trigReaction = 0;
    int accuracy = 0;
};
struct SkinsPage {
    bool agent = false, gloves = false, knife = true;
    int knifeModel = 3;
    bool enabled = true, stattrak = false, both = false, filter = true;
    float quality = 100, seed = 0;
    std::string search = "dop";
    int skin = 0;
    float listScroll = 0;
};
static RagePage g_rage;
static LegitPage g_legit;
static SkinsPage g_skins;
static const std::vector<std::string> kHitboxes = {"Head", "Chest", "Stomach", "Arms", "Legs", "Feet", "Nearest"};
static const std::vector<std::string> kLegitHitboxes = {"Head", "Chest", "Stomach", "Arms", "Legs"};
static const std::vector<std::string> kWeaponGroups = {"Global", "Auto Sniper", "SSG 08", "AWP", "R8 Revolver",
                                                       "Desert Eagle", "Pistol", "Rifle", "SMG", "Shotgun", "Machine gun"};

static void PageRage() {
    RagePage& r = g_rage;
    const float L = 97, R = kRightX, W = kGroupW;
    const float wtH = 62;  // measured
    BeginGroup("Weapon type", L, kTop, W, wtH);
    WeaponHeader(&r.weapon, r.weaponOn, kWeaponGroups);
    float cy;

    BeginGroup("Aimbot", L, kTop + wtH + 20, W, kBottom - (kTop + wtH + 20));
    cy = Checkbox("Enabled", &r.enabled);
    KeyBind(cy, &r.enabledKey);
    MultiCombo(r.hitbox, kHitboxes);
    Label("Multi-point");
    Combo(&r.multiPoint, {"-", "Low", "Medium", "High"});
    Slider(&r.multiScale, 24, 100, "%.0f%%");
    Label("Minimum hit chance");
    Slider(&r.hitChance, 0, 100, "%.0f%%");
    Label("Minimum damage");
    Slider(&r.minDamage, 0, 126, "%.0f");
    cy = Checkbox("Minimum hit chance override", &r.mhcOverride);
    KeyBind(cy, &r.mhcKey);
    cy = Checkbox("Minimum damage override", &r.mdOverride);
    KeyBind(cy, &r.mdKey);
    Checkbox("Prefer body aim", &r.preferBody);
    cy = LabelRow("Force body aim");
    KeyBind(cy, &r.forceBodyKey);
    cy = Checkbox("Quick stop", &r.quickStop);
    KeyBind(cy, &r.quickStopKey);
    Checkbox("Auto scope", &r.autoScope);

    const float otherH = 286;
    BeginGroup("Other", R, kTop, W, otherH);
    Label("Accuracy boost");
    Combo(&r.accuracy, {"Off", "Low", "Medium", "High", "Maximum"});
    Checkbox("Automatic fire", &r.autoFire);
    Checkbox("Aim through walls", &r.throughWalls);
    Checkbox("Silent aim", &r.silent);
    Checkbox("Remove recoil", &r.removeRecoil);
    Checkbox("Remove spread", &r.removeSpread, cSpecial);
    cy = Checkbox("Rapid fire", &r.rapidFire);
    KeyBind(cy, &r.rapidKey);
    cy = Checkbox("Quick peek assist", &r.quickPeek);
    KeyBind(cy, &r.peekKey);
    cy = Checkbox("Duck peek assist", &r.duckPeek);
    KeyBind(cy, &r.duckKey);
    Checkbox("Limit aim step", &r.limitStep);
    Label("Maximum FOV");
    Slider(&r.maxFov, 1, 180, "%.0f\xC2\xB0");

    BeginGroup("Anti-aimbot angles", R, kTop + otherH + 20, W, kBottom - (kTop + otherH + 20));
    cy = Checkbox("Enabled", &r.aaEnabled);
    KeyBind(cy, &r.aaKey);
    Label("Pitch");
    Combo(&r.pitch, {"Off", "Down", "Up", "Minimal", "Random"});
    Label("Yaw");
    Combo(&r.yawBase, {"Local view", "At targets"});
    Slider(&r.yawAdd, -180, 180, "%.0f\xC2\xB0");
    cy = Checkbox("Jitter", &r.aaJitter);
    KeyBind(cy, &r.jitterKey);
    cy = Checkbox("Spin", &r.aaSpin);
    KeyBind(cy, &r.spinKey);
    cy = Checkbox("Freestanding", &r.freestanding);
    KeyBind(cy, &r.fsKey);
    Slider(&r.fsOffset, -180, 180, "%.0f\xC2\xB0");
    Label("Body yaw");
    Combo(&r.bodyYaw, {"Off", "Opposite", "Jitter", "Static"});
    Slider(&r.bodyYawAmt, -180, 180, "%.0f\xC2\xB0");
    Label("Fake yaw limit");
    Slider(&r.fakeLimit, 0, 60, "%.0f\xC2\xB0");
    Checkbox("Edge yaw", &r.edgeYaw);
}

static void PageLegit() {
    LegitPage& l = g_legit;
    const float L = 97, R = kRightX, W = kGroupW;
    BeginGroup("Weapon type", L, kTop, W * 2 + 21, 79);  // measured: 79 tall
    IconTabs(g_weapons, 6, &l.weapon, 0);

    const float top = kTop + 79 + 20;
    BeginGroup("Aimbot", L, top, W, kBottom - top);
    float cy = Checkbox("Enabled", &l.enabled);
    KeyBind(cy, &l.key);
    MultiCombo(l.hitbox, kLegitHitboxes);
    Label("Maximum FOV");
    Slider(&l.fov, 0, 180, "%.1f\xC2\xB0", 0.1f);
    Label("Speed");
    Slider(&l.speed, 0, 100, "%.2f", 0.01f);
    Label("Speed (in attack)");
    Slider(&l.speedAttack, 0, 100, "%.2f", 0.01f);
    Label("Speed scale - FOV");
    Slider(&l.speedScale, 0, 100, "%.0f%%");
    Label("Reaction time");
    Slider(&l.reaction, 0, 200, "%.0fms");
    Label("Maximum lock-on time");
    Slider(&l.lockOn, 0, 1000, "%.0fms", 10.f, true);
    Label("Recoil compensation (P/Y)");
    Slider(&l.rcsP, 0, 100, "%.0f%%");
    Slider(&l.rcsY, 0, 100, "%.0f%%");
    Checkbox("Quick stop", &l.quickStop);
    Checkbox("Aim through smoke", &l.smoke);
    Checkbox("Aim while blind", &l.blind);

    const float trigH = 300;
    BeginGroup("Triggerbot", R, top, W, trigH);
    cy = Checkbox("Enabled", &l.trigEnabled);
    KeyBind(cy, &l.trigKey);
    MultiCombo(l.trigHitbox, kLegitHitboxes);
    Label("Minimum hit chance");
    Slider(&l.trigHitChance, 0, 100, "%.0f%%");
    Label("Minimum damage");
    Slider(&l.trigDamage, 1, 100, "%.0f");
    Label("Reaction time");
    Slider(&l.trigReaction, 0, 200, "%.0fms");
    Checkbox("Burst fire", &l.burst);
    Checkbox("Shoot through walls", &l.trigWalls);
    Checkbox("Shoot through smoke", &l.trigSmoke);
    Checkbox("Shoot while blind", &l.trigBlind);

    BeginGroup("Other", R, top + trigH + 20, W, kBottom - (top + trigH + 20));
    Label("Accuracy boost");
    Combo(&l.accuracy, {"Off", "Low", "Medium", "High"});
    Checkbox("Standalone recoil compensation", &l.standalone);
}

static void PageSkins() {
    SkinsPage& k = g_skins;
    const float L = 97, R = kRightX, W = kGroupW;
    BeginGroup("Model customization (CT)", L, kTop, W, kBottom - kTop);
    Checkbox("Agent", &k.agent);
    Checkbox("Gloves", &k.gloves);
    Checkbox("Knife", &k.knife);
    if (k.knife)
        Combo(&k.knifeModel, {"Bayonet", "Flip Knife", "Gut Knife", "Butterfly Knife", "Karambit", "M9 Bayonet",
                              "Huntsman Knife", "Falchion Knife", "Bowie Knife", "Shadow Daggers", "Talon Knife"});

    BeginGroup("Weapon skin", R, kTop, W, kBottom - kTop);
    Checkbox("Enabled", &k.enabled);
    Checkbox("StatTrak", &k.stattrak);
    Label("Quality");
    Slider(&k.quality, 0, 100, "%.0f%%");
    Label("Seed");
    Slider(&k.seed, 0, 1000, "%.0f");
    Checkbox("Apply to both teams", &k.both, 0, true);
    Checkbox("Filter by weapon", &k.filter);
    TextField(&k.search);
    static const char* kSkins[] = {"Doppler (Phase 1)", "Doppler (Phase 2)", "Doppler (Phase 3)", "Doppler (Phase 4)",
                                   "Doppler (Blackpearl)", "Doppler (Ruby)", "Doppler (Sapphire)",
                                   "Gamma Doppler (Phase 1)", "Gamma Doppler (Phase 2)", "Gamma Doppler (Phase 3)",
                                   "Gamma Doppler (Phase 4)", "Gamma Doppler (Emerald)", "Fade", "Marble Fade",
                                   "Tiger Tooth", "Damascus Steel", "Rust Coat", "Ultraviolet", "Slaughter",
                                   "Crimson Web", "Case Hardened", "Blue Steel", "Night", "Stained", "Urban Masked"};
    std::vector<std::pair<std::string, ImU32>> list;
    std::string q = k.search;
    for (auto& ch : q) ch = (char)tolower((unsigned char)ch);
    for (const char* sk : kSkins) {
        std::string lower = sk;
        for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
        if (q.empty() || lower.find(q) != std::string::npos) list.push_back({sk, RGB(205, 82, 112)});
    }
    if (k.skin >= (int)list.size()) k.skin = 0;
    ListBox(list, &k.skin, &k.listScroll);
}

struct MiscPage {
    bool afk = true, loadout = true, panorama = true, knifebot = true, autoWeapons = true, quickSwitch = true,
         quickPlant = true, superToss = true, autoGrenade = true, knifeSide = true, hitSound = true,
         headshotSound = false, killSound = false, autoBuy = true, clanTag = false, logDealt = true, logTaken = false,
         logPurchases = false, persistentFeed = false;
    Bind knifeKey, lastSecondKey, grenadeKey;
    int knifeMode = 0, hitSoundKind = 0, buyPrimary = 1, buySecondary = 0, buyUtility = 0;
    float autoDelay = 0, grenadeRelease = 42, hitVolume = 60;
    Col dealtCol = MakeCol(0xF2F26E), takenCol = MakeCol(0xA0A050);
    // movement
    bool bhop = true, airStrafe = true, subTick = true, avoid = true, jumpBug = true, noFall = true, noLand = false,
         airDuck = true, ramp = true, jumpEdge = true, stopEdge = true, easyStrafe = true, fastWalk = true,
         fastLadder = true, standaloneStop = true, slowMo = true;
    bool strafeSel[3] = {true, true, false};
    float subTickAmt = 100, slowMoAmt = 75;
    Bind jumpBugKey{ImGuiKey_LeftCtrl}, noLandKey, rampKey, jumpEdgeKey{ImGuiKey_E}, stopEdgeKey{ImGuiKey_LeftAlt},
        slowMoKey{ImGuiKey_LeftShift};
    // matchmaking
    bool autoAccept = false, overrideRegion = false, revealRanks = true;
};
struct ConfigPage {
    std::vector<std::string> presets = {"hvh", "legit"};
    int preset = 0;
    std::string name = "hvh";
    Bind menuKey{ImGuiKey_Delete};
    Col menuCol = MakeCol(0x95D454);
    float animSpeed = 100;
    bool confirmations = true, streamer = false;
    int dpi = 0, untrusted = 0;
};
static MiscPage g_misc2;
static ConfigPage g_config;

static void PageMisc() {
    MiscPage& m = g_misc2;
    const float L = 97, R = kRightX, W = kGroupW;
    BeginGroup("Miscellaneous", L, kTop, W, kBottom - kTop);
    Checkbox("Prevent AFK kick", &m.afk);
    Checkbox("Unlock loadout", &m.loadout);
    Checkbox("Panorama UI extensions", &m.panorama);
    float cy = Checkbox("Knifebot", &m.knifebot);
    KeyBind(cy, &m.knifeKey);
    Combo(&m.knifeMode, {"Full stab", "Quick stab", "Default"});
    Checkbox("Automatic weapons", &m.autoWeapons);
    Slider(&m.autoDelay, 0, 1000, "%.0fms");
    Checkbox("Quick switch", &m.quickSwitch);
    Checkbox("Quick plant", &m.quickPlant);
    cy = LabelRow("Last second objective");
    KeyBind(cy, &m.lastSecondKey);
    Checkbox("Super toss", &m.superToss);
    cy = Checkbox("Automatic grenade release", &m.autoGrenade);
    KeyBind(cy, &m.grenadeKey);
    Slider(&m.grenadeRelease, 0, 50, "%.0f");
    Checkbox("Switch knife viewmodel side", &m.knifeSide);
    Checkbox("Hit sound", &m.hitSound);
    Combo(&m.hitSoundKind, {"Default", "Click", "Bell", "Cod", "Bubble"});
    Slider(&m.hitVolume, 0, 330, "%.0f%%");
    Checkbox("Headshot sound", &m.headshotSound);
    Checkbox("Kill sound", &m.killSound);
    Checkbox("Auto buy", &m.autoBuy);
    Combo(&m.buyPrimary, {"-", "SSG 08", "AWP", "Auto sniper", "AK-47 / M4"});
    Combo(&m.buySecondary, {"-", "Desert Eagle / R8", "Dual Berettas", "P250", "Five-SeveN / Tec-9"});
    Combo(&m.buyUtility, {"-", "Grenades", "Armor", "Defuse kit", "Taser"});
    Checkbox("Clan tag spammer", &m.clanTag, 0, true);
    cy = Checkbox("Log damage dealt", &m.logDealt);
    ColorButtons(cy, {m.dealtCol.v});
    cy = Checkbox("Log damage taken", &m.logTaken);
    ColorButtons(cy, {m.takenCol.v});
    Checkbox("Log weapon purchases", &m.logPurchases);
    Checkbox("Persistent kill feed", &m.persistentFeed);

    const float movH = 372;
    BeginGroup("Movement", R, kTop, W, movH);
    Checkbox("Bunny hop", &m.bhop);
    Checkbox("Air strafe", &m.airStrafe);
    MultiCombo(m.strafeSel, {"View angles", "Movement keys", "Smooth"});
    Checkbox("Sub-tick air strafe", &m.subTick);
    Slider(&m.subTickAmt, 0, 100, "%.0f%%");
    Checkbox("Avoid collisions", &m.avoid);
    cy = Checkbox("Jump bug", &m.jumpBug);
    KeyBind(cy, &m.jumpBugKey);
    Checkbox("No fall damage", &m.noFall);
    cy = Checkbox("No land inaccuracy", &m.noLand);
    KeyBind(cy, &m.noLandKey);
    Checkbox("Air duck", &m.airDuck);
    cy = Checkbox("Ramp boost", &m.ramp);
    KeyBind(cy, &m.rampKey);
    cy = Checkbox("Jump at edge", &m.jumpEdge);
    KeyBind(cy, &m.jumpEdgeKey);
    cy = Checkbox("Stop at edge", &m.stopEdge);
    KeyBind(cy, &m.stopEdgeKey);
    Checkbox("Easy strafe", &m.easyStrafe);
    Checkbox("Fast walk", &m.fastWalk);
    Checkbox("Fast ladder", &m.fastLadder);
    Checkbox("Standalone quick stop", &m.standaloneStop);
    cy = Checkbox("Slow motion", &m.slowMo);
    KeyBind(cy, &m.slowMoKey);
    Slider(&m.slowMoAmt, 0, 100, "%.0f%%");

    BeginGroup("Matchmaking", R, kTop + movH + 20, W, kBottom - (kTop + movH + 20));
    Checkbox("Auto-accept matchmaking", &m.autoAccept);
    Checkbox("Override matchmaking region", &m.overrideRegion);
    Checkbox("Reveal matchmaking ranks", &m.revealRanks);
}

static void PageConfig() {
    ConfigPage& c = g_config;
    const float L = 97, R = kRightX, W = kGroupW;
    BeginGroup("Presets", L, kTop, W, kBottom - kTop);
    PresetList(c.presets, &c.preset, 118);
    NameField(&c.name);
    if (Button("Load") && c.preset < (int)c.presets.size()) c.name = c.presets[c.preset];
    if (Button("Save") && !c.name.empty()) {
        auto it = std::find(c.presets.begin(), c.presets.end(), c.name);
        if (it == c.presets.end()) { c.presets.push_back(c.name); c.preset = (int)c.presets.size() - 1; }
        else c.preset = (int)(it - c.presets.begin());
    }
    if (Button("Delete") && c.preset < (int)c.presets.size()) {
        c.presets.erase(c.presets.begin() + c.preset);
        c.preset = std::max(0, c.preset - 1);
    }
    if (Button("Reset")) { g_rage = RagePage(); g_legit = LegitPage(); g_misc2 = MiscPage(); }
    if (Button("Import from clipboard")) {}
    if (Button("Export to clipboard")) ImGui::SetClipboardText(c.name.c_str());

    BeginGroup("Settings", R, kTop, W, kBottom - kTop);
    float cy = LabelRow("Menu key");
    KeyBind(cy, &c.menuKey);
    cy = LabelRow("Menu color");
    ColorButtons(cy, {accent});
    Label("Menu animation speed");
    Slider(&c.animSpeed, 0, 200, "%.0f%%");
    Checkbox("Menu confirmations", &c.confirmations);
    Label("DPI scale");
    g_comboDisabled = true;
    Combo(&c.dpi, {"Automatic", "100%", "125%", "150%", "200%"});
    Label("Anti-untrusted");
    Combo(&c.untrusted, {"Automatic", "On", "Off"});
    g_comboDisabled = false;
    Checkbox("Streamer mode", &c.streamer);
}

static Tex g_botAvatar;
struct PlayersPage {
    std::string search, overrideName;
    bool enemiesOpen = true, teamOpen = true;
    int sel = 0;  // index into the combined list
    float listScroll = 0;
    struct Adj { bool whitelist = false, priority = false, noVisuals = false, noModel = false, body = false,
                 hitbox = false, pitch = false, noForce = false; Bind pitchKey; };
    std::unordered_map<std::string, Adj> adj;
    Col whitelistCol = MakeCol(0x95D454), priorityCol = MakeCol(0xD42020);
};
static PlayersPage g_players;
static const char* kEnemies[] = {"BOT Aim Botz Recoil Master", "BOT Aim Botz Soaphy", "BOT Aim Botz Freddie",
                                 "BOT Aim Botz Inferno", "BOT Aim Botz Mirage", "BOT Aim Botz Assault",
                                 "BOT Aim Botz Italy", "BOT Aim Botz uLLeticaL", "BOT Aim Botz Cheater",
                                 "BOT Aim Botz Overpass", "BOT Aim Botz Dust", "BOT Aim Botz Overwatcher"};
static const char* kTeammates[] = {"wrecked (You)"};

static void PagePlayers() {
    PlayersPage& pl = g_players;
    const float L = 97, R = kRightX, W = kGroupW;
    BeginGroup("Players", L, kTop, W, kBottom - kTop);
    float x = G.x + 22, w = G.w - 38;
    InputBoxAt(&pl.search, "Search...", x, G.y + 20, w, 18, false);
    // tree list: collapsible Enemies / Teammates
    float ly = G.y + 41, lh = G.y + G.h - 46 - ly;
    Fill(x - 1, ly - 1, w + 2, lh + 2, Hex(0x0F0F0F));
    Fill(x, ly, w, lh, Hex(0x1C1C1C));
    std::string q = pl.search;
    for (auto& ch : q) ch = (char)tolower((unsigned char)ch);
    auto match = [&](const char* nm) {
        if (q.empty()) return true;
        std::string l = nm;
        for (auto& ch : l) ch = (char)tolower((unsigned char)ch);
        return l.find(q) != std::string::npos;
    };
    const float ih = 17.f;
    D->PushClipRect(P(x, ly), P(x + w, ly + lh), true);
    float y = ly + 3;
    std::string selName;
    auto header = [&](const char* title, bool* open) {
        bool hov;
        if (Clicked(x, y, w, ih, &hov)) *open = !*open;
        float tx = x + 9, ty = y + ih / 2;
        if (*open) D->AddTriangleFilled(P(tx - 3, ty - 1.5f), P(tx + 3, ty - 1.5f), P(tx, ty + 2), Hex(0xC8C8C8));
        else D->AddTriangleFilled(P(tx - 1.5f, ty - 3), P(tx + 2, ty), P(tx - 1.5f, ty + 3), Hex(0xC8C8C8));
        Text(x + 17, ty, title, RGB(215, 215, 215), true);
        y += ih;
    };
    int idx = 0;
    auto item = [&](const char* nm) {
        int me = idx++;
        if (!match(nm)) return;
        bool hov;
        if (Clicked(x, y, w, ih, &hov)) pl.sel = me;
        bool on = pl.sel == me;
        if (hov || on) Fill(x, y, w, ih, on ? Hex(0x232323) : Hex(0x202020));
        Text(x + 17, y + ih / 2, nm, on ? RGB(235, 235, 235) : RGB(170, 170, 170), on);
        if (on) selName = nm;
        y += ih;
    };
    header("Enemies", &pl.enemiesOpen);
    for (const char* nm : kEnemies) { if (pl.enemiesOpen) item(nm); else idx++; }
    header("Teammates", &pl.teamOpen);
    for (const char* nm : kTeammates) { if (pl.teamOpen) item(nm); else idx++; }
    D->PopClipRect();
    if (selName.empty()) selName = pl.sel < 12 ? kEnemies[std::clamp(pl.sel, 0, 11)] : kTeammates[0];
    if (ButtonAt("Reset all", x, G.y + G.h - 33, w, 20, true)) {}

    // information
    const float infoH = 104;
    BeginGroup("Information", R, kTop, W, infoH);
    if (g_botAvatar.id) {
        float ax = G.x + 18, ay = G.y + 22;
        D->AddImage((ImTextureID)(intptr_t)g_botAvatar.id, P(ax, ay), P(ax + 59, ay + 59));
    }
    Text(G.x + 82, G.y + 28, selName.c_str(), RGB(220, 220, 220), true);
    bool bot = selName.rfind("BOT ", 0) == 0;
    Text(G.x + 82, G.y + 42, bot ? "Bot Difficulty: 0" : "Local player", Hex(0xA8A8A8));

    // adjustments
    const float actH = 104, adjY = kTop + infoH + 20, adjH = kBottom - actH - 20 - adjY;
    BeginGroup("Adjustments", R, adjY, W, adjH);
    PlayersPage::Adj& a = pl.adj[selName];
    G.cy += 4;
    Text(RowX(), G.cy + 7.5f, "Override player name", Hex(0x4E4E4E));
    G.cy += 16;
    InputBoxAt(&pl.overrideName, "Override player name", RowX(), G.cy, ControlW(), 18, true);
    G.cy += 24;
    float cy = Checkbox("Add to whitelist", &a.whitelist);
    ColorButtons(cy, {pl.whitelistCol.v});
    cy = Checkbox("High priority", &a.priority);
    ColorButtons(cy, {pl.priorityCol.v});
    Checkbox("Disable visuals", &a.noVisuals);
    Checkbox("Disable model rendering", &a.noModel);
    Checkbox("Prefer body aim", &a.body);
    Checkbox("Override hitbox selection", &a.hitbox);
    cy = Checkbox("Override pitch", &a.pitch);
    KeyBind(cy, &a.pitchKey);
    Checkbox("Disable force remove spread", &a.noForce, 0, true);

    // actions
    BeginGroup("Actions", R, kBottom - actH, W, actH);
    G.cy += 6;
    if (Button("Steal player name")) pl.overrideName = selName;
    Button("Vote kick", true);
}

static void DrawPage() {
    if (g_tab == 5) { PagePlayers(); return; }
    if (g_tab == 3) { PageMisc(); return; }
    if (g_tab == 6) { PageConfig(); return; }
    if (g_tab == 0) { PageRage(); return; }
    if (g_tab == 1) { PageLegit(); return; }
    if (g_tab == 4) { PageSkins(); return; }
    if (g_tab == 2) {
        if (g_category <= 1) PageEsp(g_esp[g_category], g_category == 1);
        else {
            SimplePage& p = g_visOther[g_category - 2];
            Column(p.left, 97, p.a, p.av, p.as, p.ac);
            Column(p.right, kRightX, p.b, p.bv, p.bs, p.bc);
        }
    } else {
        SimplePage& p = g_tab == 3 ? g_misc : g_pages[g_tab];
        Column(p.left, 97, p.a, p.av, p.as, p.ac);
        Column(p.right, kRightX, p.b, p.bv, p.bs, p.bc);
    }
}

static Tex g_side[7], g_cat[4];

static void DrawMenu() {
    const float L = kOrigin.x, T = kOrigin.y, R = L + kW, B = T + kH;

    ImU32 frame[6] = {cBlack, Hex(0x3C3C3C), Hex(0x282828), Hex(0x282828), Hex(0x282828), Hex(0x3C3C3C)};
    for (int i = 0; i < 6; i++) Fill(L + i, T + i, kW - 2 * i, kH - 2 * i, frame[i]);
    const float CX = L + 6, CY = T + 6, CW = kW - 12, CH = kH - 12;

    D->AddImage((ImTextureID)(intptr_t)g_bgTex.id, P(CX, CY), P(CX + CW, CY + CH), ImVec2(0, 0),
                ImVec2(CW * S / 4.f, CH * S / 4.f));

    Fill(CX, CY, CW, 1, Hex(0x050505));
    float mid = CX + CW * 0.5f;
    HGrad(CX, CY + 1, mid - CX, 1, Hex(0x608B99), Hex(0x854E87));
    HGrad(mid, CY + 1, CX + CW - mid, 1, Hex(0x854E87), Hex(0xB1B580));
    HGrad(CX, CY + 2, mid - CX, 1, Hex(0x43727F), Hex(0x733B76));
    HGrad(mid, CY + 2, CX + CW - mid, 1, Hex(0x733B76), Hex(0x90956A));
    Fill(CX, CY + 3, CW, 1, Hex(0x050505));

    float sbTop = CY + 4;
    Fill(CX, sbTop, 74 - CX, CY + CH - sbTop, cSidebar);
    Fill(74, sbTop, 1, CY + CH - sbTop, Hex(0x000000));
    Fill(75, sbTop, 1, CY + CH - sbTop, Hex(0x262626));
    for (int i = 0; i < 7; i++) {
        float ty = 17 + i * 60;
        bool sel = g_tab == i, hov;
        if (Clicked(CX, ty, 75 - CX, 60, &hov) && g_tab != i) {
            g_tab = i;
            ClosePopup();
        }
        if (sel) {
            Fill(CX, ty - 1, 76 - CX, 1, Hex(0x000000));
            Fill(CX, ty, 76 - CX, 1, Hex(0x262626));
            D->AddImage((ImTextureID)(intptr_t)g_bgTex.id, P(CX, ty + 1), P(76, ty + 59),
                        ImVec2(0, 0), ImVec2((76 - CX) * S / 4.f, 58 * S / 4.f));
            Fill(CX, ty + 59, 76 - CX, 1, Hex(0x262626));
            Fill(CX, ty + 60, 76 - CX, 1, Hex(0x000000));
        }
        Icon(g_side[i], 40, ty + 30, 44, sel ? IM_COL32_WHITE : (hov ? Hex(0xC8C8C8) : Hex(0x9A9A9A)));
    }

    if (g_tab == 2) {
        BeginGroup("Category", 97, 27, kGroupW * 2 + 21, 56);
        for (int i = 0; i < 4; i++) {
            float cx = 140 + i * 71.3f;
            bool hov;
            if (Clicked(cx - 30, 30, 60, 50, &hov) && g_category != i) {
                g_category = i;
                ClosePopup();
            }
            Icon(g_cat[i], cx, 55, 40, g_category == i ? Hex(0xF0F0F0) : (hov ? Hex(0xC8C8C8) : Hex(0x9F9F9F)));
        }
    }
    DrawPage();
    EndGroup();
}

static void CaptureKeyBind() {
    if (!g_waiting) return;
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { g_waiting->key = 0; g_waiting->waiting = false; g_waiting = nullptr; return; }
    if (ImGui::IsMouseClicked(3)) { g_waiting->key = -1; }
    else if (ImGui::IsMouseClicked(4)) { g_waiting->key = -2; }
    else {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++)
            if (k != ImGuiKey_MouseLeft && ImGui::IsKeyPressed((ImGuiKey)k, false) && !(k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY)) {
                g_waiting->key = k;
                break;
            }
        if (!g_waiting->key && !ImGui::IsMouseClicked(1) && !ImGui::IsMouseClicked(2)) return;
        if (ImGui::IsMouseClicked(1)) g_waiting->key = ImGuiKey_MouseRight;
        if (ImGui::IsMouseClicked(2)) g_waiting->key = ImGuiKey_MouseMiddle;
    }
    (void)io;
    g_waiting->waiting = false;
    g_waiting = nullptr;
}

static std::string ExeDir(const char* argv0) {
    std::string p = argv0;
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? "." : p.substr(0, s);
}

int main(int argc, char** argv) {
    const char* shot = nullptr;
    bool native = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--native")) native = true;
        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) g_tab = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) S = std::max(0.5f, (float)atof(argv[++i]));
    }
    g_assets = ExeDir(argv[0]) + "/assets";

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    if (shot) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    if (shot && native) S = 16.f / 9.f;
    int ww = (int)std::round(kW * S), wh = (int)std::round(kH * S);
    GLFWwindow* win = glfwCreateWindow(ww, wh, "gamesense", nullptr, nullptr);
    if (!win) return 1;
    if (vm) glfwSetWindowPos(win, (vm->width - ww) / 2, (vm->height - wh) / 2);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    BuildGdiFont(g_font[0], 12, false, S);
    BuildGdiFont(g_font[1], 12, true, S);

    static const char* side[7] = {"rage", "legit", "visuals", "misc", "skins", "players", "config"};
    static const char* cat[4] = {"cat_enemy", "cat_team", "cat_world", "cat_eye"};
    for (int i = 0; i < 7; i++) g_side[i] = LoadTex(side[i]);
    for (int i = 0; i < 4; i++) g_cat[i] = LoadTex(cat[i]);
    static const char* weap[6] = {"wl_pistol", "wt_smg", "wt_rifle", "wt_shotgun", "wt_mg", "wt_sniper"};
    static const char* wl[11] = {"wl_global", "wl_auto", "wl_ssg", "wl_awp", "wl_r8", "wl_deagle", "wl_pistol",
                                 "wl_rifle", "wt_smg", "wt_shotgun", "wt_mg"};
    for (int i = 0; i < 11; i++) g_weaponList[i] = LoadTex(wl[i]);
    for (int i = 0; i < 6; i++) g_weapons[i] = LoadTex(weap[i]);
    g_weaponGlobal = LoadTex("w_global");
    g_botAvatar = LoadTex("bot_avatar");
    MakeBackgroundTexture();

    bool dragging = false;
    double dragX = 0, dragY = 0;
    int frame = 0;
    bool first = true;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (shot) io.MousePos = ImVec2((181 - kOrigin.x) * S, (370 - kOrigin.y) * S);
        g_click = ImGui::IsMouseClicked(0);
        g_blocked = false;
        g_overWidget = false;
        CaptureKeyBind();
        if (!g_waiting && !g_editing && !g_editing2 && !g_editing3 && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (g_pop.kind != POP_NONE) ClosePopup();
            else glfwSetWindowShouldClose(win, 1);
        }
        PopupInput();

        D = ImGui::GetBackgroundDrawList();
        DrawMenu();
        if (first && g_tab == 2 && getenv("SKEET_OPEN_FLAGS")) {
            first = false;
            float y = 102 + 17 + 6 * 18;
            g_pop.kind = POP_MULTI; g_pop.owner = g_esp[0].flagSel; g_pop.flags = g_esp[0].flagSel; g_pop.items = kFlags;
            g_pop.x = 134; g_pop.y = y; g_pop.w = std::min(kGroupW - 93, 180.f); g_pop.h = kComboH; g_pop.scroll = 5;
        }
        static bool openWeapons = getenv("SKEET_OPEN_WEAPONS") != nullptr;  // (debug) screenshot with the list open
        if (openWeapons && g_tab == 0) {
            openWeapons = false;
            g_pop.kind = POP_WEAPON; g_pop.owner = &g_rage.weapon; g_pop.index = &g_rage.weapon; g_pop.items = kWeaponGroups;
            g_pop.enabled = g_rage.weaponOn; g_pop.icons = g_weaponList;
            g_pop.x = 97; g_pop.y = kTop; g_pop.w = kGroupW; g_pop.h = 62;
        }
        PopupDraw();

        if (ImGui::IsMouseClicked(0) && !g_overWidget && !g_blocked && g_click) {
            dragging = true;
            glfwGetCursorPos(win, &dragX, &dragY);
        }
        if (!io.MouseDown[0]) dragging = false;
        if (dragging) {
            int wx, wy;
            double cx, cy;
            glfwGetWindowPos(win, &wx, &wy);
            glfwGetCursorPos(win, &cx, &cy);
            glfwSetWindowPos(win, wx + (int)(cx - dragX), wy + (int)(cy - dragY));
        }

        ImGui::Render();
        int fw, fh;
        glfwGetFramebufferSize(win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (shot && ++frame == 4) {
            std::vector<unsigned char> px((size_t)fw * fh * 4);
            glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            FILE* f = fopen(shot, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", fw, fh);
                for (int y = fh - 1; y >= 0; y--)
                    for (int x = 0; x < fw; x++) fwrite(&px[((size_t)y * fw + x) * 4], 1, 3, f);
                fclose(f);
            }
            break;
        }
        glfwSwapBuffers(win);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
