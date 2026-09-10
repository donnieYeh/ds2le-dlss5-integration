// DINPUT8.dll forwarding proxy for DS2LE. CRT-free (linked with GNU ld).
// Forwards all dinput8 exports to dinput8_orig.dll (system copy) and loads the
// two DLSS5 add-ons as plain DLLs so no second ReShade instance is needed.
// The loader thread waits for LE's dxgi.dll (the ReShade host) to appear first,
// then EXTENDS dxgi.dll's in-memory export table with the 13 ReShade API
// functions LE's build lacks, and REDIRECTS the 6 native ReShade exports that
// are stubs (RegisterAddon/GetConfigValue/SetConfigValue/UnregisterAddon) or
// unusable without a populated addon registry (RegisterEvent/UnregisterEvent)
// to real implementations in this proxy. LE dispatches addon events through a
// single-slot array at dxgi+0xBFB030 (86 slots); a watchdog re-applies our
// registrations if LE's one-time load_builtin_addons zeroes the array after
// the addons have already registered.
#include <windows.h>
#include <commctrl.h>

// CRT-free: satisfy cl's loop-idiom memcpy/memset emissions
#pragma function(memcpy, memset)
void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d; const unsigned char *ss = (const unsigned char *)s;
    size_t i; for (i = 0; i < n; i++) dd[i] = ss[i]; return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *dd = (unsigned char *)d; size_t i;
    for (i = 0; i < n; i++) dd[i] = (unsigned char)c; return d;
}

static int wlen(const wchar_t *s) { int n = 0; while (s[n]) n++; return n; }
static void wcpy(wchar_t *d, const wchar_t *s) { while ((*d++ = *s++)); }
static void wcat(wchar_t *d, const wchar_t *s) { while (*d) d++; while ((*d++ = *s++)); }
static wchar_t *wrchr(wchar_t *s, wchar_t c) {
    wchar_t *last = NULL;
    while (*s) { if (*s == c) last = s; s++; }
    return last;
}

static void hex64(char *out, unsigned long long v) {
    static const char h[] = "0123456789ABCDEF";
    int i;
    out[0] = '0'; out[1] = 'x';
    for (i = 0; i < 16; i++) out[2 + i] = h[(v >> ((15 - i) * 4)) & 0xF];
    out[18] = 0;
}

static void logmsg(const char *tag, const wchar_t *name, unsigned long long val) {
    wchar_t exedir[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t *s;
    HANDLE f;
    DWORD written;
    char buf[640];
    char hx[24];
    int n = 0, i;
    SYSTEMTIME st;

    GetModuleFileNameW(NULL, exedir, MAX_PATH);
    s = wrchr(exedir, L'\\');
    if (s) *s = 0;
    wcpy(path, exedir);
    wcat(path, L"\\dlss5-loader.log");

    f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;

    GetLocalTime(&st);
    // "HH:MM:SS  tag name 0xVAL\r\n"
    buf[n++] = (char)('0' + st.wHour / 10);   buf[n++] = (char)('0' + st.wHour % 10);
    buf[n++] = ':';
    buf[n++] = (char)('0' + st.wMinute / 10); buf[n++] = (char)('0' + st.wMinute % 10);
    buf[n++] = ':';
    buf[n++] = (char)('0' + st.wSecond / 10); buf[n++] = (char)('0' + st.wSecond % 10);
    buf[n++] = ' '; buf[n++] = ' ';
    for (i = 0; tag[i]; i++) buf[n++] = tag[i];
    if (name) {
        buf[n++] = ' ';
        while (*name && n < 500) {
            wchar_t c = *name++;
            buf[n++] = (c < 128) ? (char)c : '?';
        }
    }
    buf[n++] = ' ';
    hex64(hx, val);
    for (i = 0; hx[i]; i++) buf[n++] = hx[i];
    buf[n++] = '\r'; buf[n++] = '\n';

    WriteFile(f, buf, (DWORD)n, &written, NULL);
    CloseHandle(f);
}

static void logmsgA(const char *tag, const char *name, unsigned long long val) {
    wchar_t w[300];
    int i = 0;
    if (name) {
        while (name[i] && i < 299) { w[i] = (wchar_t)(unsigned char)name[i]; i++; }
    }
    w[i] = 0;
    logmsg(tag, name ? w : NULL, val);
}

// ---------------- ReShade API shim (EAT extension of LE's dxgi.dll) ----------

static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static char g_basepath[520];
static int  g_basepath_len;

// ReShadeGetBasePath(char *buffer, size_t *length) -- semantics mirrored from
// ReShade 6.8 disassembly: len==NULL -> 0; buf==NULL -> *len=pathlen+1;
// else copy min(*len-1, pathlen) bytes, NUL-terminate, *len=copied.
static unsigned long long shim_GetBasePath(char *buf, unsigned long long *len) {
    unsigned long long cap, n;
    int i;
    if (!len) return 0;
    if (!buf) { *len = (unsigned long long)g_basepath_len + 1; return 1; }
    cap = *len;
    if (cap == 0) { *len = 0; return 1; }
    n = cap - 1;
    if ((unsigned long long)g_basepath_len < n) n = (unsigned long long)g_basepath_len;
    for (i = 0; i < (int)n; i++) buf[i] = g_basepath[i];
    buf[n] = 0;
    *len = n;
    return 1;
}

// ReShadeGetImGuiFunctionTable(version): LE's dxgi.dll has NO real imgui
// function table (the whole addon-imgui-table subsystem is compiled out;
// static scan: no ~700-entry table exists, the lone 1101-entry .rdata run
// is the CRT initializer array, and the version constants 19000/19250
// appear nowhere in its code). So return a fake table whose entries all
// point at a stub that zeroes rax+xmm0 and returns, except entry[0]
// (ImGui::GetVersion) which answers a real version string so the addons'
// version gates pass. Overlay draw calls therefore harmlessly no-op.
static unsigned long long shim_ret0(void);
static void *g_fake_imgui[1536];
static int g_fake_imgui_ready;
static void *shim_GetImGuiTable(unsigned long long a, unsigned long long b,
                                unsigned long long c, unsigned long long d) {
    int i;
    unsigned char *page;
    logmsg("shim GetImGuiFunctionTable", NULL, a);
    if (!g_fake_imgui_ready) {
        page = (unsigned char *)VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (page) {
            // stub @page+0: xor eax,eax; xorps xmm0,xmm0; ret
            page[0] = 0x33; page[1] = 0xC0; page[2] = 0x0F; page[3] = 0x57;
            page[4] = 0xC0; page[5] = 0xC3;
            // stub @page+16: lea rax,[rip+1]; ret; "1.92.5" at page+24
            page[16] = 0x48; page[17] = 0x8D; page[18] = 0x05;
            page[19] = 0x01; page[20] = 0; page[21] = 0; page[22] = 0;
            page[23] = 0xC3;
            page[24] = '1'; page[25] = '.'; page[26] = '9'; page[27] = '2';
            page[28] = '.'; page[29] = '5'; page[30] = 0;
            for (i = 0; i < 1536; i++) g_fake_imgui[i] = (void *)page;
            g_fake_imgui[0] = (void *)(page + 16);
        }
        g_fake_imgui_ready = 1;
    }
    (void)b; (void)c; (void)d;
    return g_fake_imgui;
}

static void shim_RegisterOverlay(const char *title, void *cb) {
    (void)cb;
    logmsg("shim RegisterOverlay", NULL, title ? (unsigned long long)(ULONG_PTR)title : 0);
}
static void shim_UnregisterOverlay(const char *title, void *cb) {
    (void)title; (void)cb;
}

// generic stubs for the remaining ReShade 6.8 API surface LE's build lacks
static unsigned long long shim_ret0(void) { return 0; }
static unsigned long long shim_ret1(void) { return 1; }
static void shim_void(void) {}
static const char *shim_Version(void) { return "6.8.0.2155"; }

// --------- real addon-API replacements for LE's stubbed native exports ------
// LE's dxgi.dll: ReShadeRegisterAddon and ReShadeGetConfigValue share one
// `mov al,1; ret` stub @0x13720, ReShadeSetConfigValue/ReShadeUnregisterAddon
// share another stub @0xF490. The external *.addon64 loader is compiled out,
// so the addon registry never gains a node and the (real) native
// ReShadeRegisterEvent fails every call with "Could not find associated
// add-on ...". Event invocation in this build reads a single-slot dispatch
// array at dxgi+0xBFB030 (86 qwords) and calls the slot directly, so a
// faithful registry is unnecessary: we write the dispatch array ourselves.

static unsigned char *g_dxgi;
static void **g_dispatch;              // g_dxgi + 0xBFB030
static char g_inipath[520];            // <game dir>\ReShade.ini

static CRITICAL_SECTION g_evlk;
static int g_evlk_ready;
// Helper threads are intentionally detached from the loader thread.  They
// must therefore observe shutdown before the host starts unloading dxgi or
// renodx, not only when this proxy itself finally receives PROCESS_DETACH.
static volatile LONG g_exiting;

static int is_exiting(void) {
    return InterlockedCompareExchange(&g_exiting, 0, 0) != 0;
}

static void mark_exiting(void) {
    InterlockedExchange(&g_exiting, 1);
}

// bisection switches: [DLSS5Proxy] EventsBridge/EventsRenodx/EventsDxgi
// (default 1). A blocked module still gets TRUE so its init proceeds.
static int g_allow_bridge = 1, g_allow_renodx = 1, g_allow_dxgi = 1;
static int g_allow_loaded;

static void load_allow_flags(void) {
    char b[8];
    if (g_allow_loaded) return;
    g_allow_loaded = 1;
    if (GetPrivateProfileStringA("DLSS5Proxy", "EventsBridge", "1", b, sizeof b, g_inipath))
        g_allow_bridge = (b[0] != '0');
    if (GetPrivateProfileStringA("DLSS5Proxy", "EventsRenodx", "1", b, sizeof b, g_inipath))
        g_allow_renodx = (b[0] != '0');
    if (GetPrivateProfileStringA("DLSS5Proxy", "EventsDxgi", "1", b, sizeof b, g_inipath))
        g_allow_dxgi = (b[0] != '0');
}

static void mod_basename(HMODULE m, char *out, int cap);

static int str_contains(const char *h, const char *needle) {
    int i, j;
    for (i = 0; h[i]; i++) {
        for (j = 0; needle[j]; j++) {
            if (h[i + j] != needle[j]) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static int module_allowed(HMODULE mod, char *nm, int cap) {
    mod_basename(mod, nm, cap);
    if (!g_allow_loaded) load_allow_flags();
    if (str_contains(nm, "bridge")) return g_allow_bridge;
    if (str_contains(nm, "renodx")) return g_allow_renodx;
    return g_allow_dxgi;
}

// LE's dispatch is single-slot-per-event, but several parties register the
// same event (LE's own built-in runtime code, the bridge, renodx). Real
// ReShade invokes every registered callback, so we emulate that: N>1
// registrants -> slot points at a per-event thunk that fans out in
// registration order. Callbacks are MS-x64 and take up to 5 args
// (4 registers + 1 stack dword, e.g. event 0x4C).
#define MAXCB 8
typedef struct _CBENT { void *cb; HMODULE module; } CBENT;
static CBENT g_cbs[0x56][MAXCB];
static int g_ncbs[0x56];
static unsigned char *g_thunkpage;     // per-event thunks + fanout entry
#define FANOUT_OFF 0x600

typedef void (*EVCB5)(unsigned long long, unsigned long long, unsigned long long,
                      unsigned long long, unsigned long long);

static void my_fanout(unsigned int event, unsigned long long a1, unsigned long long a2,
                      unsigned long long a3, unsigned long long a4, unsigned long long a5) {
    void *cbs[MAXCB];
    int n = 0, i;
    EnterCriticalSection(&g_evlk);
    if (event < 0x56) {
        n = g_ncbs[event];
        for (i = 0; i < n; i++) cbs[i] = g_cbs[event][i].cb;
    }
    LeaveCriticalSection(&g_evlk);
    for (i = 0; i < n; i++) {
        if (is_exiting()) break;
        ((EVCB5)cbs[i])(a1, a2, a3, a4, a5);
    }
}

// thunk: mov r10d,<ev>; jmp fanout_entry   (fits in 16 bytes)
// fanout_entry forwards rcx,rdx,r8,r9 and the caller's stack arg5 unchanged.
static int build_fanout_page(void) {
    static const unsigned char entry_code[] = {
        0x48, 0x8B, 0x44, 0x24, 0x28,             // mov rax,[rsp+0x28]   a5
        0x4C, 0x8B, 0xD9,                         // mov r11,r9           a4
        0x4D, 0x8B, 0xC8,                         // mov r9,r8            a3
        0x4C, 0x8B, 0xC2,                         // mov r8,rdx           a2
        0x48, 0x8B, 0xD1,                         // mov rdx,rcx          a1
        0x4C, 0x89, 0xD1,                         // mov rcx,r10          event
        0x48, 0x83, 0xEC, 0x38,                   // sub rsp,0x38
        0x4C, 0x89, 0x5C, 0x24, 0x20,             // mov [rsp+0x20],r11   a4 -> stack arg5
        0x48, 0x89, 0x44, 0x24, 0x28,             // mov [rsp+0x28],rax   a5 -> stack arg6
        0x48, 0xB8, 0,0,0,0,0,0,0,0,              // mov rax,&my_fanout
        0xFF, 0xD0,                               // call rax
        0x48, 0x83, 0xC4, 0x38,                   // add rsp,0x38
        0xC3                                      // ret
    };
    g_thunkpage = (unsigned char *)VirtualAlloc(NULL, 0x1000,
                                                MEM_RESERVE | MEM_COMMIT,
                                                PAGE_EXECUTE_READWRITE);
    if (!g_thunkpage) return -1;
    memcpy(g_thunkpage + FANOUT_OFF, entry_code, sizeof entry_code);
    *(void **)(g_thunkpage + FANOUT_OFF + 0x24) = (void *)&my_fanout;
    return 0;
}

static void *ensure_thunk(unsigned int ev) {
    unsigned char *t = g_thunkpage + ev * 16;
    if (t[0] != 0x41) {
        long rel;
        t[0] = 0x41; t[1] = 0xBA;               // mov r10d, imm32
        t[2] = (unsigned char)(ev & 0xFF);
        t[3] = (unsigned char)((ev >> 8) & 0xFF);
        t[4] = (unsigned char)((ev >> 16) & 0xFF);
        t[5] = (unsigned char)((ev >> 24) & 0xFF);
        t[6] = 0xE9;                            // jmp rel32
        rel = (long)((g_thunkpage + FANOUT_OFF) - (t + 11));
        t[7]  = (unsigned char)(rel & 0xFF);
        t[8]  = (unsigned char)((rel >> 8) & 0xFF);
        t[9]  = (unsigned char)((rel >> 16) & 0xFF);
        t[10] = (unsigned char)((rel >> 24) & 0xFF);
        t[11] = (unsigned char)0xCC; t[12] = 0xCC; t[13] = 0xCC;
        t[14] = 0xCC; t[15] = 0xCC;
    }
    return t;
}

// caller holds g_evlk
static void *expected_slot(unsigned int ev) {
    if (g_ncbs[ev] == 0) return NULL;
    if (g_ncbs[ev] == 1) return g_cbs[ev][0].cb;
    return ensure_thunk(ev);
}

static void clear_all_event_slots(void) {
    int ev;
    if (!g_evlk_ready || !g_dispatch) return;
    EnterCriticalSection(&g_evlk);
    memset(g_cbs, 0, sizeof g_cbs);
    memset(g_ncbs, 0, sizeof g_ncbs);
    for (ev = 0; ev < 0x56; ev++) g_dispatch[ev] = NULL;
    LeaveCriticalSection(&g_evlk);
}

// ReShadeRegisterEvent(uint32 event, void *callback) -> bool
static unsigned long long my_RegisterEvent(unsigned int event, void *cb) {
    HMODULE mod = NULL;
    char nm[160];
    int i;
    if (is_exiting() || event >= 0x56 || !cb) return 0;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (const char *)cb, &mod);
    mod_basename(mod, nm, sizeof nm);
    if (!module_allowed(mod, nm, sizeof nm)) {
        logmsgA("evt BLOCKED", nm, event);
        return 1;   // lie: let the addon believe registration worked
    }
    EnterCriticalSection(&g_evlk);
    for (i = 0; i < g_ncbs[event]; i++) {
        if (g_cbs[event][i].cb == cb && g_cbs[event][i].module == mod) break;
    }
    if (i == g_ncbs[event]) {
        if (g_ncbs[event] >= MAXCB) { LeaveCriticalSection(&g_evlk); return 0; }
        g_cbs[event][i].cb = cb;
        g_cbs[event][i].module = mod;
        g_ncbs[event]++;
    }
    g_dispatch[event] = expected_slot(event);
    LeaveCriticalSection(&g_evlk);
    mod_basename(mod, nm, sizeof nm);
    logmsgA("evt reg", nm, event);
    return 1;
}

// ReShadeUnregisterEvent(uint32 event, void *callback)
static void my_UnregisterEvent(unsigned int event, void *cb) {
    int i;
    char nm[160];
    HMODULE mod = NULL;
    if (event >= 0x56 || !cb) return;
    EnterCriticalSection(&g_evlk);
    for (i = 0; i < g_ncbs[event]; i++) {
        if (g_cbs[event][i].cb == cb) {
            mod = g_cbs[event][i].module;
            g_cbs[event][i] = g_cbs[event][g_ncbs[event] - 1];
            g_ncbs[event]--;
            break;
        }
    }
    g_dispatch[event] = expected_slot(event);
    LeaveCriticalSection(&g_evlk);
    mod_basename(mod, nm, sizeof nm);
    // LE unregisters its dxgi callbacks as part of the final host teardown.
    // Stop all detached helpers at this earlier, still-valid boundary.  The
    // external add-ons are not present in LE's native addon list, so their
    // redirected callbacks would otherwise survive until unload_addons() and
    // trip its addon_event_list[idx] == nullptr assertion. Clear the complete
    // proxy table before that host assertion runs.
    if (str_contains(nm, "dxgi")) {
        mark_exiting();
        clear_all_event_slots();
    }
    logmsgA("evt unreg", nm, event);
}

static void mod_basename(HMODULE m, char *out, int cap) {
    char full[MAX_PATH];
    int n, b;
    if (cap <= 0) return;
    out[0] = 0;
    if (!m) { out[0] = '?'; out[1] = 0; return; }
    n = (int)GetModuleFileNameA(m, full, MAX_PATH);
    if (n <= 0) { out[0] = '?'; out[1] = 0; return; }
    full[n] = 0;
    b = n;
    while (b > 0 && full[b - 1] != '\\' && full[b - 1] != '/') b--;
    n = 0;
    while (full[b] && n < cap - 1) out[n++] = full[b++];
    out[n] = 0;
}

// ReShadeRegisterAddon(HMODULE module, uint32 api_version) -> bool
static unsigned long long my_RegisterAddon(HMODULE module, unsigned int ver) {
    char nm[160];
    if (is_exiting()) return 0;
    mod_basename(module, nm, sizeof nm);
    logmsgA("addon reg", nm, ver);
    return 1;
}

// ReShadeUnregisterAddon(HMODULE module)
static void my_UnregisterAddon(HMODULE module) {
    char nm[160];
    int ev, i;
    mod_basename(module, nm, sizeof nm);

    // The host's unload_addons() asserts that every dispatch slot is empty
    // after external add-ons have unregistered.  Since RegisterEvent is
    // redirected into our fan-out table, the host cannot remove these
    // callbacks itself; do the same bookkeeping here before the module can
    // be unloaded.  Removing by module also covers an add-on that skipped its
    // individual UnregisterEvent calls during an Alt+F4 teardown.
    if (module) {
        EnterCriticalSection(&g_evlk);
        for (ev = 0; ev < 0x56; ev++) {
            for (i = 0; i < g_ncbs[ev]; ) {
                if (g_cbs[ev][i].module == module) {
                    g_cbs[ev][i] = g_cbs[ev][g_ncbs[ev] - 1];
                    g_ncbs[ev]--;
                    continue;
                }
                i++;
            }
            g_dispatch[ev] = expected_slot((unsigned int)ev);
        }
        LeaveCriticalSection(&g_evlk);
    }
    logmsgA("addon unreg", nm, 0);
    if (str_contains(nm, "renodx") || str_contains(nm, "bridge"))
        mark_exiting();
}

// ReShadeGetConfigValue(void *module, void *zero, const char *section,
//                       const char *key, char *buf, size_t *len) -> bool
// (arg order lifted from renodx v4.6 call sites: rcx=module, rdx=0,
//  r8=section, r9=key, stack: buf, &len; value is returned as text)
static unsigned long long my_GetConfigValue(unsigned long long module,
                                            unsigned long long zero,
                                            const char *section, const char *key,
                                            char *buf, unsigned long long *plen) {
    DWORD got;
    unsigned long long cap;
    char logv[220];
    int i;
    (void)module; (void)zero;
    if (is_exiting() || !section || !key || !buf || !plen) return 0;
    cap = *plen;
    if (cap == 0) return 0;
    if (cap > 4000) cap = 4000;
    got = GetPrivateProfileStringA(section, key, "", buf, (DWORD)cap, g_inipath);
    *plen = got;
    // "section.key=value" truncated into the log line
    for (i = 0; section[i] && i < 100; i++) logv[i] = section[i];
    logv[i++] = '.';
    { int j = 0; while (key[j] && i < 150) logv[i++] = key[j++]; }
    logv[i++] = '=';
    { DWORD j = 0; while (j < got && i < 200) logv[i++] = buf[j++]; }
    logv[i] = 0;
    logmsgA("cfg get", logv, got ? 1 : 0);
    return got ? 1 : 0;
}

// ReShadeSetConfigValue(void *module, const char *value, const char *section,
//                       const char *key, ...)
static void my_SetConfigValue(unsigned long long module, const char *value,
                              const char *section, const char *key,
                              unsigned long long a5) {
    int ok = 0, n = 0;
    (void)module; (void)a5;
    if (is_exiting() || !section || !key) return;
    __try {
        if (value) {
            while (n < 120 && value[n]) {
                unsigned char c = (unsigned char)value[n];
                if (c < 0x20 || c > 0x7E) break;
                n++;
            }
            ok = (n > 0 && n < 120 && value[n] == 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = 0; }
    if (ok) {
        char logv[260];
        int i = 0, j;
        while (section[i] && i < 80) { logv[i] = section[i]; i++; }
        logv[i++] = '.';
        j = 0; while (key[j] && i < 140) logv[i++] = key[j++];
        logv[i++] = '=';
        j = 0; while (value[j] && i < 240) logv[i++] = value[j++];
        logv[i] = 0;
        WritePrivateProfileStringA(section, key, value, g_inipath);
        logmsgA("cfg set", logv, 1);
    } else {
        logmsgA("cfg set REJECTED (value not a printable string)", section,
                (unsigned long long)(ULONG_PTR)value);
    }
}

// ---------------- live config poke (ADR-0008) ----------------
// renodx v4.6 stores its 19 ini keys in module globals (map proven via the
// startup loader's prologue loads + epilogue xchg stores, cross-checked both
// ways). The shader-side knobs are read every frame, so writing these globals
// takes effect live. This thread re-reads [RenoDX.DLSS5] once a second and
// pokes only changed keys. Create-time keys (NREnableUpscaling, EnableHooks)
// are written too but only bind at next launch.

#define POKE_I32  0
#define POKE_F32  1
#define POKE_BOOL 2

typedef struct _POKEENT {
    const char *key;
    unsigned int rva;       // offset from renodx-dlss5.addon64 base (v4.6, sha256 245c0613)
    int type;
    float fmin, fmax;       // f32 clamp (from the loader's own epilogue maxss/minss)
    long imin, imax;        // i32 clamp
    const char *def;        // plugin's own default (compile-time .data bytes / loader slot init)
    char last[64];          // last-seen ini text (snapshot, no poke at start)
} POKEENT;

static POKEENT g_poke[] = {
    { "NeuralUplift",       0x18bf18, POKE_BOOL, 0, 0, 0, 1,    "1",   "" },
    { "NREnableUpscaling",  0x18f68d, POKE_BOOL, 0, 0, 0, 1,    "0",   "" },
    { "NRPreset",           0x18f6e0, POKE_I32,  0, 0, 0, 3,    "0",   "" },
    { "NRStyle",            0x18f77c, POKE_I32,  0, 0, 0, 2,    "0",   "" },
    { "NRIntensity",        0x18c664, POKE_F32,  0.0f, 2.0f, 0, 0, "1",   "" },
    { "NRLocalTone",        0x18c668, POKE_F32,  0.0f, 2.0f, 0, 0, "1",   "" },
    { "NRLocalStructure",   0x18c66c, POKE_F32,  0.0f, 2.0f, 0, 0, "1",   "" },
    { "NRSkinStructure",    0x18c670, POKE_F32, -1.0f, 2.0f, 0, 0, "-1",  "" },
    { "NRAutoMask",         0x18f778, POKE_BOOL, 0, 0, 0, 1,    "0",   "" },
    { "NRUICorrection",     0x18f780, POKE_BOOL, 0, 0, 0, 1,    "0",   "" },
    { "NRDepthMode",        0x18f774, POKE_I32,  0, 0, 0, 2,    "0",   "" },
    { "NRMVecScaleX",       0x18c65c, POKE_F32, -2.0f, 2.0f, 0, 0, "1",   "" },
    { "NRMVecScaleY",       0x18c660, POKE_F32, -2.0f, 2.0f, 0, 0, "1",   "" },
    { "NRPaperWhiteScale",  0x18c650, POKE_F32,  0.05f, 16.0f, 0, 0, "1", "" },
    { "NRTransferStrength", 0x18c654, POKE_F32,  0.0f, 1.0f, 0, 0, "1",   "" },
    { "NRColorStrength",    0x18c658, POKE_F32,  0.0f, 1.0f, 0, 0, "1",   "" },
    { "NRToggleKey",        0x18c67c, POKE_I32,  0, 0, 8, 255,  "117", "" },
    { "NRScreenshotKey",    0x18c678, POKE_I32,  0, 0, 8, 255,  "116", "" },
    // EnableHooks is stored as TWO derived bytes: 0x18c674 = (v != 0),
    // 0x18c675 = (v == 1); loader slot init 2 when the ini key is absent.
    { "EnableHooks",        0x18c674, POKE_BOOL, 0, 0, 0, 2,    "2",   "" },
};
#define NPOKE (sizeof(g_poke) / sizeof(g_poke[0]))

// CRT-free parsers: optional sign, decimal digits, one '.', no exponent.
static long parse_i32(const char *s, int *ok) {
    long v = 0, sign = 1;
    int nd = 0;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; nd++; }
    *ok = nd > 0;
    return sign * v;
}

static float parse_f32(const char *s, int *ok) {
    float v = 0.0f, scale = 0.1f, sign = 1.0f;
    int nd = 0;
    if (*s == '-') { sign = -1.0f; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s - '0'); s++; nd++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { v += scale * (float)(*s - '0'); scale *= 0.1f; s++; nd++; }
    }
    *ok = nd > 0;
    return sign * v;
}

// v4.6 keeps these globals in .data (RW), but flip the covering page(s) to RWX
// around the write anyway: an earlier build of this table pointed at RX .text
// (two AV-write crashes at base+0x842f4), and the restore keeps the cost nil.
// The SEH/finally guard is deliberate: shutdown can race the 1-second watcher,
// and a stale module address must fail closed instead of taking the game down.
static int poke_write(void *addr, const void *src, unsigned int sz) {
    unsigned char *p0 = (unsigned char *)((ULONG_PTR)addr & ~(ULONG_PTR)0xFFF);
    unsigned char *p1 = (unsigned char *)(((ULONG_PTR)addr + sz - 1) & ~(ULONG_PTR)0xFFF);
    SIZE_T len = (SIZE_T)(p1 - p0) + 0x1000;
    DWORD oldp = 0;
    unsigned int i;
    int changed = 0, ok = 0;
    if (!addr || !src || sz == 0) return 0;
    __try {
        if (!VirtualProtect(p0, len, PAGE_EXECUTE_READWRITE, &oldp)) __leave;
        changed = 1;
        for (i = 0; i < sz; i++)
            ((unsigned char *)addr)[i] = ((const unsigned char *)src)[i];
        ok = 1;
    } __finally {
        if (changed) {
            __try { VirtualProtect(p0, len, oldp, &oldp); }
            __except (EXCEPTION_EXECUTE_HANDLER) { }
        }
    }
    return ok;
}

static void poke_one(unsigned char *base, POKEENT *e, const char *txt) {
    int ok = 0;
    logmsgA("poke", e->key, (unsigned long long)(ULONG_PTR)(base + e->rva)); // intent first: attributable on crash
    if (e->type == POKE_I32 || e->type == POKE_BOOL) {
        long v = parse_i32(txt, &ok);
        if (!ok) return;
        if (v < e->imin) v = e->imin;
        if (v > e->imax) v = e->imax;
        if (e->type == POKE_BOOL) {
            unsigned char b = (unsigned char)(v ? 1 : 0);
            if (!poke_write(base + e->rva, &b, 1)) { logmsgA("poke write fail", e->key, 0); return; }
            if (e->rva == 0x18c674) { // EnableHooks: secondary byte = (v == 1)
                b = (unsigned char)(v == 1 ? 1 : 0);
                if (!poke_write(base + 0x18c675, &b, 1)) { logmsgA("poke write fail", e->key, 1); return; }
            }
        } else {
            if (!poke_write(base + e->rva, &v, 4)) { logmsgA("poke write fail", e->key, 0); return; }
        }
        logmsgA("poke ok", e->key, (unsigned long long)v);
    } else {
        float v = parse_f32(txt, &ok);
        if (!ok) return;
        if (v < e->fmin) v = e->fmin;
        if (v > e->fmax) v = e->fmax;
        if (!poke_write(base + e->rva, &v, 4)) { logmsgA("poke write fail", e->key, 0); return; }
        logmsgA("poke ok", e->key, (unsigned long long)(unsigned int)(v * 1000.0f)); // milli-value in log
    }
}

// Win32 caches ini files per process: after renodx's startup read, external
// edits are invisible to GetPrivateProfileStringA in this process (verified
// live). So the watcher reads and parses the file itself.
static char g_ini_text[65536];

static int cfg_read_file(const char *path, char *buf, long cap) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    DWORD n = 0, total = 0;
    if (h == INVALID_HANDLE_VALUE) return -1;
    while (total < (DWORD)(cap - 1) && ReadFile(h, buf + total, cap - 1 - total, &n, NULL) && n)
        total += n;
    CloseHandle(h);
    buf[total] = 0;
    return (int)total;
}

// Extract key's value from ini text while inside [section]. Returns length, 0 if absent/empty.
static int ini_extract(const char *text, const char *section, const char *key, char *out, int cap) {
    const char *p = text;
    int in_sec = 0;
    out[0] = 0;
    while (*p) {
        const char *eol = p;
        const char *s;
        while (*eol && *eol != '\n') eol++;
        s = p;
        while (s < eol && (*s == ' ' || *s == '\t' || *s == '\r')) s++;
        if (*s == '[') {
            const char *c = ++s;
            const char *sec = section;
            in_sec = 0;
            while (c < eol && *c != ']' && *sec) {
                char ca = *c, cb = *sec;
                if (ca >= 'A' && ca <= 'Z') ca += 32;
                if (cb >= 'A' && cb <= 'Z') cb += 32;
                if (ca != cb) break;
                c++; sec++;
            }
            if (!*sec && c < eol && *c == ']') in_sec = 1;
        } else if (in_sec && *s != ';' && *s != '#') {
            const char *k = key;
            const char *c = s;
            while (*k && c < eol) {
                char ca = *c, cb = *k;
                if (ca >= 'A' && ca <= 'Z') ca += 32;
                if (cb >= 'A' && cb <= 'Z') cb += 32;
                if (ca != cb) break;
                c++; k++;
            }
            if (!*k) {
                int len;
                while (c < eol && (*c == ' ' || *c == '\t')) c++;
                if (c < eol && *c == '=') {
                    c++;
                    while (c < eol && (*c == ' ' || *c == '\t')) c++;
                    len = 0;
                    while (c < eol && *c != '\r' && len < cap - 1) out[len++] = *c++;
                    while (len > 0 && (out[len-1] == ' ' || out[len-1] == '\t')) len--;
                    out[len] = 0;
                    return len;
                }
            }
        }
        p = *eol ? eol + 1 : eol;
    }
    return 0;
}

static DWORD WINAPI cfgwatch_thread(LPVOID p) {
    HMODULE rn = NULL;
    unsigned char *base;
    int i;
    char buf[64];
    (void)p;
    for (i = 0; i < 240 && !rn; i++) {
        if (is_exiting()) return 0;
        rn = GetModuleHandleA("renodx-dlss5.addon64");
        if (!rn) Sleep(500);
    }
    if (is_exiting() || !rn) { logmsg("cfgwatch: renodx not found", NULL, 0); return 0; }
    base = (unsigned char *)rn;
    logmsg("cfgwatch: armed", NULL, (unsigned long long)(ULONG_PTR)base);
    // snapshot current ini state: renodx's own loader already applied it
    if (cfg_read_file(g_inipath, g_ini_text, sizeof g_ini_text) > 0) {
        for (i = 0; i < (int)NPOKE; i++)
            ini_extract(g_ini_text, "RenoDX.DLSS5", g_poke[i].key,
                        g_poke[i].last, sizeof g_poke[i].last);
    }
    for (;;) {
        HMODULE pin = NULL;
        Sleep(1000);
        if (is_exiting()) return 0;
        // renodx can be FreeLibrary'd during host shutdown while we are alive;
        // its VA range may then be reused by another module. Never poke a stale base.
        if (GetModuleHandleA("renodx-dlss5.addon64") != (HMODULE)base) {
            logmsg("cfgwatch: renodx unloaded, stop", NULL, 0);
            return 0;
        }
        // Pin the exact module containing the target address for the complete
        // scan. GetModuleHandleA above is only an observation and does not
        // prevent FreeLibrary from racing us.
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                (const char *)(base + 1), &pin) || pin != (HMODULE)base) {
            if (pin) FreeLibrary(pin);
            logmsg("cfgwatch: cannot pin renodx, stop", NULL, 0);
            return 0;
        }
        if (is_exiting()) { FreeLibrary(pin); return 0; }
        if (cfg_read_file(g_inipath, g_ini_text, sizeof g_ini_text) <= 0) {
            FreeLibrary(pin);
            continue;
        }
        for (i = 0; i < (int)NPOKE; i++) {
            POKEENT *e = &g_poke[i];
            if (is_exiting()) break;
            ini_extract(g_ini_text, "RenoDX.DLSS5", e->key, buf, sizeof buf);
            if (!buf[0]) { e->last[0] = 0; continue; } // key absent: leave default alone
            if (scmp(buf, e->last) == 0) continue;
            { int j = 0; while (buf[j] && j < 63) { e->last[j] = buf[j]; j++; } e->last[j] = 0; }
            __try {
                poke_one(base, e, buf);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                logmsgA("cfgwatch: poke exception", e->key, GetExceptionCode());
                mark_exiting();
                break;
            }
        }
        if (!is_exiting()) FreeLibrary(pin);
    }
    return 0;
}

// ---------------- F2 floating config panel (ADR-0008 phase 2) ----------------
// Separate topmost Win32 window, deliberately NOT hooked into the D3D path.
// F2 toggles it. Edits write ReShade.ini; cfgwatch pokes renodx globals within
// ~1s, so this panel is just a frontend for the poke layer. Panel text is ASCII
// on purpose (UTF-8 source + A-API mixing is not worth it here).

#define PCTL_EDIT   0
#define PCTL_CHECK  1
#define PCTL_COMBO  2
#define PCTL_SEP    3
#define PCTL_SLIDER 4

typedef struct _PNLROW {
    int poke;           // index into g_poke (-1 for separator)
    int ctrl;
    const char *hint;
    const char *items;  // combo items, '|'-separated
} PNLROW;

static PNLROW g_rows[] = {
    { 3,  PCTL_COMBO,  "0=Default 1=Natural 2=Cinema", "0 Default|1 Natural|2 Cinema" }, // NRStyle
    { 2,  PCTL_COMBO,  "preset 0-3",        "0|1|2|3" }, // NRPreset
    { 10, PCTL_COMBO,  "0-2",               "0|1|2" },   // NRDepthMode
    { 4,  PCTL_SLIDER, 0, 0 },   // NRIntensity        [0, 2]
    { 5,  PCTL_SLIDER, 0, 0 },   // NRLocalTone        [0, 2]
    { 6,  PCTL_SLIDER, 0, 0 },   // NRLocalStructure   [0, 2]
    { 7,  PCTL_SLIDER, 0, 0 },   // NRSkinStructure    [-1, 2]
    { 11, PCTL_SLIDER, 0, 0 },   // NRMVecScaleX       [-2, 2]
    { 12, PCTL_SLIDER, 0, 0 },   // NRMVecScaleY       [-2, 2]
    { 13, PCTL_SLIDER, 0, 0 },   // NRPaperWhiteScale  [0.05, 16]
    { 14, PCTL_SLIDER, 0, 0 },   // NRTransferStrength [0, 1]
    { 15, PCTL_SLIDER, 0, 0 },   // NRColorStrength    [0, 1]
    { 8,  PCTL_CHECK, "", 0 },             // NRAutoMask
    { 9,  PCTL_CHECK, "", 0 },             // NRUICorrection
    { -1, PCTL_SEP,   "-- below: restart-needed or handler-mediated (no live effect) --", 0 },
    { 0,  PCTL_CHECK, "while running use F6 instead", 0 }, // NeuralUplift
    { 1,  PCTL_CHECK, "restart needed", 0 },   // NREnableUpscaling
    { 18, PCTL_CHECK, "restart needed (0/1/2; checkbox = 0/2)", 0 }, // EnableHooks
    { 16, PCTL_EDIT,  "VK code, restart (F6=117)", 0 }, // NRToggleKey
    { 17, PCTL_EDIT,  "VK code, restart (F5=116)", 0 }, // NRScreenshotKey
};
#define NROWS (sizeof(g_rows) / sizeof(g_rows[0]))
#define PNL_W 560
#define ROW_H 26
#define ID_APPLY 1099
#define ID_RESET 1098
#define ID_ROW   1000

static HWND g_pnl, g_pnl_ctl[NROWS], g_pnl_val[NROWS]; // val = slider readout label
static int g_pnl_visible;
static char g_panel_ini[65536]; // separate from cfgwatch's g_ini_text (threads)

static void i2a10(long v, char *out) {
    char t[16];
    int n = 0, i = 0;
    unsigned long u;
    if (v < 0) { out[i++] = '-'; u = (unsigned long)(-v); } else u = (unsigned long)v;
    do { t[n++] = (char)('0' + (u % 10)); u /= 10; } while (u);
    while (n) out[i++] = t[--n];
    out[i] = 0;
}

// float -> "x.xx" (2 decimals, rounded; CRT-free)
static void f2a(float v, char *out) {
    long c = (long)(v * 100.0f + (v >= 0.0f ? 0.5f : -0.5f));
    long ip = c / 100, fp = c % 100;
    int n = 0;
    if (fp < 0) fp = -fp;
    if (c < 0 && ip == 0) out[n++] = '-';
    i2a10(ip, out + n);
    while (out[n]) n++;
    out[n++] = '.';
    out[n++] = (char)('0' + fp / 10);
    out[n++] = (char)('0' + fp % 10);
    out[n] = 0;
}

// sliders map [fmin, fmax] onto integer ticks of 0.01
static int slider_ticks(int row) {
    POKEENT *e = &g_poke[g_rows[row].poke];
    return (int)((e->fmax - e->fmin) * 100.0f + 0.5f);
}
static int slider_pos_of(int row, float v) {
    POKEENT *e = &g_poke[g_rows[row].poke];
    float t = (v - e->fmin) * 100.0f;
    int p;
    if (t < 0.0f) t = 0.0f;
    p = (int)(t + 0.5f);
    if (p > slider_ticks(row)) p = slider_ticks(row);
    return p;
}
static float slider_val_of(int row, int pos) {
    return g_poke[g_rows[row].poke].fmin + (float)pos * 0.01f;
}

static void panel_write_row(int i, const char *val) {
    WritePrivateProfileStringA("RenoDX.DLSS5", g_poke[g_rows[i].poke].key, val, g_inipath);
}

static void panel_apply_all(void) {
    int i;
    char b[64];
    for (i = 0; i < (int)NROWS; i++) {
        if (g_rows[i].ctrl != PCTL_EDIT) continue;
        GetWindowTextA(g_pnl_ctl[i], b, sizeof b);
        if (b[0]) panel_write_row(i, b);
    }
}

static void panel_refresh(void) {
    int i;
    char b[64];
    if (cfg_read_file(g_inipath, g_panel_ini, sizeof g_panel_ini) <= 0) return;
    for (i = 0; i < (int)NROWS; i++) {
        PNLROW *r = &g_rows[i];
        POKEENT *e;
        if (r->ctrl == PCTL_SEP) continue;
        e = &g_poke[r->poke];
        ini_extract(g_panel_ini, "RenoDX.DLSS5", e->key, b, sizeof b);
        if (!b[0]) { // key absent in ini -> show the plugin's own default
            int j = 0;
            while (e->def[j] && j < 63) { b[j] = e->def[j]; j++; }
            b[j] = 0;
        }
        if (r->ctrl == PCTL_EDIT) {
            SetWindowTextA(g_pnl_ctl[i], b);
        } else if (r->ctrl == PCTL_CHECK) {
            int ok = 0;
            long v = parse_i32(b, &ok);
            SendMessageA(g_pnl_ctl[i], BM_SETCHECK, (ok && v) ? BST_CHECKED : BST_UNCHECKED, 0);
        } else if (r->ctrl == PCTL_COMBO) {
            int ok = 0;
            long v = parse_i32(b, &ok);
            SendMessageA(g_pnl_ctl[i], CB_SETCURSEL, ok ? (WPARAM)v : (WPARAM)-1, 0);
        } else if (r->ctrl == PCTL_SLIDER) {
            int ok = 0;
            float v = parse_f32(b, &ok);
            char vb[16];
            if (!ok) { v = parse_f32(e->def, &ok); }
            if (v < e->fmin) v = e->fmin;
            if (v > e->fmax) v = e->fmax;
            SendMessageA(g_pnl_ctl[i], TBM_SETPOS, 1, slider_pos_of(i, v));
            f2a(v, vb);
            SetWindowTextA(g_pnl_val[i], vb);
        }
    }
}

// Reset defaults: write every key's plugin default into the ini; cfgwatch
// pokes the changed ones live within ~1s.
static void panel_reset_defaults(void) {
    int i;
    for (i = 0; i < (int)NPOKE; i++)
        WritePrivateProfileStringA("RenoDX.DLSS5", g_poke[i].key, g_poke[i].def, g_inipath);
    logmsgA("panel", "reset defaults", (unsigned long long)NPOKE);
    panel_refresh();
}

static LRESULT CALLBACK panel_wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (is_exiting()) return 0;
    if (m == WM_HSCROLL) { // trackbar moved (drag, arrows, page) -> ini + readout
        int i;
        for (i = 0; i < (int)NROWS; i++) {
            if (g_rows[i].ctrl == PCTL_SLIDER && g_pnl_ctl[i] == (HWND)lp) {
                int pos = (int)SendMessageA(g_pnl_ctl[i], TBM_GETPOS, 0, 0);
                char vb[16];
                f2a(slider_val_of(i, pos), vb);
                panel_write_row(i, vb);
                SetWindowTextA(g_pnl_val[i], vb);
                return 0;
            }
        }
        return 0;
    }
    if (m == WM_COMMAND) {
        int id = (int)(wp & 0xFFFF), ev = (int)((wp >> 16) & 0xFFFF);
        if (id == ID_APPLY && ev == BN_CLICKED) { panel_apply_all(); return 0; }
        if (id == ID_RESET && ev == BN_CLICKED) { panel_reset_defaults(); return 0; }
        if (id >= ID_ROW && id < ID_ROW + (int)NROWS) {
            int i = id - ID_ROW;
            if (g_rows[i].ctrl == PCTL_CHECK && ev == BN_CLICKED) {
                LRESULT c = SendMessageA(g_pnl_ctl[i], BM_GETCHECK, 0, 0);
                // EnableHooks: checkbox writes its on/off defaults (2/0), not 1
                if (g_rows[i].poke == 18)
                    panel_write_row(i, c == BST_CHECKED ? "2" : "0");
                else
                    panel_write_row(i, c == BST_CHECKED ? "1" : "0");
            } else if (g_rows[i].ctrl == PCTL_COMBO && ev == CBN_SELCHANGE) {
                LRESULT s = SendMessageA(g_pnl_ctl[i], CB_GETCURSEL, 0, 0);
                if (s >= 0) { char b[8]; i2a10((long)s, b); panel_write_row(i, b); }
            }
        }
        return 0;
    }
    if (m == WM_CLOSE) { panel_apply_all(); ShowWindow(w, SW_HIDE); g_pnl_visible = 0; return 0; }
    return DefWindowProcA(w, m, wp, lp);
}

struct enumctx { DWORD pid; HWND self, found; };
static BOOL CALLBACK enum_find_game(HWND w, LPARAM lp) {
    struct enumctx *c = (struct enumctx *)lp;
    DWORD pid = 0;
    char t[16];
    GetWindowThreadProcessId(w, &pid);
    if (pid != c->pid || w == c->self || !IsWindowVisible(w)) return TRUE;
    if (GetWindowTextA(w, t, sizeof t) <= 0) return TRUE;
    c->found = w;
    return FALSE;
}

static void panel_toggle(void) {
    if (is_exiting()) return;
    if (!g_pnl) return;
    if (g_pnl_visible) {
        struct enumctx c;
        panel_apply_all();
        ShowWindow(g_pnl, SW_HIDE);
        g_pnl_visible = 0;
        c.pid = GetCurrentProcessId(); c.self = g_pnl; c.found = NULL;
        EnumWindows(enum_find_game, (LPARAM)&c);
        if (c.found) SetForegroundWindow(c.found);
    } else {
        panel_refresh();
        ShowWindow(g_pnl, SW_SHOW);
        g_pnl_visible = 1;
        SetForegroundWindow(g_pnl);
    }
}

static DWORD WINAPI panel_thread(LPVOID p) {
    WNDCLASSA wc;
    MSG msg;
    HFONT fnt;
    int f2 = 0, i, y;
    (void)p;
    if (is_exiting()) return 0;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = panel_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "DLSS5CfgPanel";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassA(&wc)) { logmsg("panel RegisterClass failed", NULL, (unsigned long long)GetLastError()); return 0; }
    InitCommonControls(); // msctls_trackbar32
    g_pnl = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "DLSS5CfgPanel",
                            "DLSS5 NR Config  (F2 toggles)",
                            WS_POPUP | WS_CAPTION | WS_SYSMENU,
                            120, 120, PNL_W, 16 + (int)NROWS * ROW_H + 120,
                            NULL, NULL, wc.hInstance, NULL);
    if (!g_pnl) { logmsg("panel create failed", NULL, (unsigned long long)GetLastError()); return 0; }
    fnt = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (i = 0; i < (int)NROWS; i++) {
        PNLROW *r = &g_rows[i];
        HWND c = NULL;
        y = 8 + i * ROW_H;
        if (r->ctrl == PCTL_SEP) {
            c = CreateWindowExA(0, "STATIC", r->hint, WS_CHILD | WS_VISIBLE,
                                10, y + 4, PNL_W - 30, 18, g_pnl, NULL, NULL, NULL);
        } else {
            HWND lab = CreateWindowExA(0, "STATIC", g_poke[r->poke].key, WS_CHILD | WS_VISIBLE,
                                       10, y + 3, 150, 18, g_pnl, NULL, NULL, NULL);
            SendMessageA(lab, WM_SETFONT, (WPARAM)fnt, 1);
            if (r->ctrl == PCTL_EDIT) {
                c = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    165, y, 90, 22, g_pnl, (HMENU)(ULONG_PTR)(ID_ROW + i), NULL, NULL);
            } else if (r->ctrl == PCTL_SLIDER) {
                int ticks = slider_ticks(i);
                HWND vl;
                c = CreateWindowExA(0, "msctls_trackbar32", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    165, y, 260, 24, g_pnl, (HMENU)(ULONG_PTR)(ID_ROW + i), NULL, NULL);
                SendMessageA(c, TBM_SETRANGE, 1, (LPARAM)(unsigned)((unsigned)ticks << 16));
                SendMessageA(c, TBM_SETPAGESIZE, 0, ticks / 10 > 0 ? ticks / 10 : 1);
                SendMessageA(c, TBM_SETLINESIZE, 0, ticks / 20 > 0 ? ticks / 20 : 1);
                vl = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE,
                                     432, y + 4, 118, 18, g_pnl, NULL, NULL, NULL);
                SendMessageA(vl, WM_SETFONT, (WPARAM)fnt, 1);
                g_pnl_val[i] = vl;
            } else if (r->ctrl == PCTL_CHECK) {
                c = CreateWindowExA(0, "BUTTON", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                    165, y + 2, 18, 18, g_pnl, (HMENU)(ULONG_PTR)(ID_ROW + i), NULL, NULL);
            } else if (r->ctrl == PCTL_COMBO) {
                const char *it = r->items;
                c = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                    165, y, 130, 200, g_pnl, (HMENU)(ULONG_PTR)(ID_ROW + i), NULL, NULL);
                while (it && *it) {
                    char item[32];
                    int n = 0;
                    while (*it && *it != '|' && n < 31) item[n++] = *it++;
                    item[n] = 0;
                    if (*it == '|') it++;
                    SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)item);
                }
            }
            if (r->hint) {
                HWND h = CreateWindowExA(0, "STATIC", r->hint, WS_CHILD | WS_VISIBLE,
                                         305, y + 3, PNL_W - 320, 18, g_pnl, NULL, NULL, NULL);
                SendMessageA(h, WM_SETFONT, (WPARAM)fnt, 1);
            }
        }
        if (c) { SendMessageA(c, WM_SETFONT, (WPARAM)fnt, 1); g_pnl_ctl[i] = c; }
    }
    {
        int by = 8 + (int)NROWS * ROW_H + 8;
        HWND b = CreateWindowExA(0, "BUTTON", "Apply VK edits", WS_CHILD | WS_VISIBLE,
                                 10, by, 100, 26, g_pnl, (HMENU)(ULONG_PTR)ID_APPLY, NULL, NULL);
        HWND rb = CreateWindowExA(0, "BUTTON", "Reset defaults", WS_CHILD | WS_VISIBLE,
                                  118, by, 110, 26, g_pnl, (HMENU)(ULONG_PTR)ID_RESET, NULL, NULL);
        HWND n2 = CreateWindowExA(0, "STATIC", "sliders / checks / combos write instantly; watcher pokes within ~1s (dlss5-loader.log 'poke')",
                                  WS_CHILD | WS_VISIBLE, 236, by + 4, PNL_W - 250, 18, g_pnl, NULL, NULL, NULL);
        HWND n3 = CreateWindowExA(0, "STATIC", "Reset writes the plugin's own defaults for all 19 keys; shown values fall back to defaults when the ini key is absent",
                                  WS_CHILD | WS_VISIBLE, 10, by + 34, PNL_W - 30, 18, g_pnl, NULL, NULL, NULL);
        SendMessageA(b, WM_SETFONT, (WPARAM)fnt, 1);
        SendMessageA(rb, WM_SETFONT, (WPARAM)fnt, 1);
        SendMessageA(n2, WM_SETFONT, (WPARAM)fnt, 1);
        SendMessageA(n3, WM_SETFONT, (WPARAM)fnt, 1);
    }
    logmsg("panel ready (F2)", NULL, 0);
    for (;;) {
        if (is_exiting()) return 0;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (is_exiting()) return 0;
            if (g_pnl && IsDialogMessageA(g_pnl, &msg)) continue; // Tab/arrows/Space navigation
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        {
            SHORT s = GetAsyncKeyState(VK_F2);
            if ((s & 0x8000) && !f2) { f2 = 1; panel_toggle(); }
            else if (!(s & 0x8000)) f2 = 0;
        }
        Sleep(50);
    }
}

// Watchdog: LE's one-time load_builtin_addons zeroes the whole dispatch array
// the first time it runs (device init). If that happens after an addon has
// registered, restore its slots.
static DWORD WINAPI watchdog_thread(LPVOID p) {
    (void)p;
    for (;;) {
        int ev;
        Sleep(300);
        if (is_exiting()) return 0;
        if (!g_evlk_ready || !g_dispatch) continue;
        EnterCriticalSection(&g_evlk);
        // DllMain/teardown may have flipped the state while we were waiting
        // for the lock. Do not touch the host's dispatch array in that case.
        if (is_exiting() || !g_evlk_ready || !g_dispatch) {
            LeaveCriticalSection(&g_evlk);
            return 0;
        }
        for (ev = 0; ev < 0x56; ev++) {
            if (g_ncbs[ev] > 0) {
                void *want = expected_slot(ev);
                if (g_dispatch[ev] != want) {
                    g_dispatch[ev] = want;
                    logmsgA("evt restore", NULL, (unsigned long long)ev);
                }
            }
        }
        LeaveCriticalSection(&g_evlk);
    }
    return 0;
}

typedef struct _SHIMDEF { const char *name; void *impl; } SHIMDEF;
#define NSHIMS 13
static const SHIMDEF g_shims[NSHIMS] = {
    { "ReShadeCreateEffectRuntime",            (void *)&shim_ret0 },
    { "ReShadeDestroyEffectRuntime",           (void *)&shim_void },
    { "ReShadeGetBasePath",                    (void *)&shim_GetBasePath },
    { "ReShadeGetImGuiFunctionTable",          (void *)&shim_GetImGuiTable },
    { "ReShadeRegisterEventForAddon",          (void *)&shim_ret1 },
    { "ReShadeRegisterOverlay",                (void *)&shim_RegisterOverlay },
    { "ReShadeRegisterOverlayForAddon",        (void *)&shim_RegisterOverlay },
    { "ReShadeSetConfigArray",                 (void *)&shim_ret1 },
    { "ReShadeUnregisterEventForAddon",        (void *)&shim_void },
    { "ReShadeUnregisterOverlay",              (void *)&shim_UnregisterOverlay },
    { "ReShadeUnregisterOverlayForAddon",      (void *)&shim_UnregisterOverlay },
    { "ReShadeUpdateAndPresentEffectRuntime",  (void *)&shim_void },
    { "ReShadeVersion",                        (void *)&shim_Version },
};

// native exports LE ships but that are stubs / unusable as-is: repoint them
// at the real implementations above (no new names, existing slots only)
#define NREDIR 6
static const SHIMDEF g_redir[NREDIR] = {
    { "ReShadeGetConfigValue",               (void *)&my_GetConfigValue },
    { "ReShadeRegisterAddon",                (void *)&my_RegisterAddon },
    { "ReShadeRegisterEvent",                (void *)&my_RegisterEvent },
    { "ReShadeSetConfigValue",               (void *)&my_SetConfigValue },
    { "ReShadeUnregisterAddon",              (void *)&my_UnregisterAddon },
    { "ReShadeUnregisterEvent",              (void *)&my_UnregisterEvent },
};

// Extends the loaded dxgi.dll export table with g_shims entries and repoints
// any export named in g_redir at our replacement (jump stubs in the same
// region). Returns 0 on success, negative on failure.
static int extend_dxgi_exports(HMODULE hmod) {
    unsigned char *base = (unsigned char *)hmod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_EXPORT_DIRECTORY *ed, *ned;
    DWORD dirRVA, NF, NN, BASE, i, k;
    DWORD nt_dirSize = 0;
    DWORD *funcs; DWORD *names; WORD *ords;
    DWORD *nfuncs; DWORD *nnames; WORD *nords;
    unsigned char *region, *p;
    SIZE_T need, step;
    unsigned long long start, limit, addr;
    DWORD oldprot;
    int order[NSHIMS];
    DWORD redir_ord[NREDIR];
    int redir_idx[NREDIR];
    int nredir_found = 0;

    logmsg("ext: enter", NULL, 0);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return -1;
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return -2;
    dirRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!dirRVA) return -3;
    logmsg("ext: pe ok dirRVA", NULL, (unsigned long long)dirRVA);
    g_dxgi = base;
    g_dispatch = (void **)(base + 0xBFB030);
    if (build_fanout_page() != 0) { logmsg("ext: fanout page fail", NULL, 0); return -7; }
    ed = (IMAGE_EXPORT_DIRECTORY *)(base + dirRVA);
    NF = ed->NumberOfFunctions;
    NN = ed->NumberOfNames;
    BASE = ed->Base;
    funcs = (DWORD *)(base + ed->AddressOfFunctions);
    names = (DWORD *)(base + ed->AddressOfNames);
    ords  = (WORD  *)(base + ed->AddressOfNameOrdinals);

    logmsg("ext: counts", NULL, ((unsigned long long)NF << 32) | NN);
    need = 0x1000 + (SIZE_T)(NF + NSHIMS) * 4 + (SIZE_T)(NN + NSHIMS) * 4 + (SIZE_T)(NN + NSHIMS) * 2 + 1024 + (NSHIMS + NREDIR) * 16;
    step = 0x10000;
    start = ((unsigned long long)base + nt->OptionalHeader.SizeOfImage + step) & ~((unsigned long long)step - 1);
    limit = (unsigned long long)base + 0xFFF00000ull;
    region = 0;
    for (addr = start; addr < limit; addr += step) {
        region = (unsigned char *)VirtualAlloc((LPVOID)addr, need, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (region) break;
    }
    if (!region) { logmsg("ext: alloc fail", NULL, 0); return -4; }
    logmsg("ext: region", NULL, (unsigned long long)(ULONG_PTR)region);

    p = region;
    ned = (IMAGE_EXPORT_DIRECTORY *)p; p += sizeof(IMAGE_EXPORT_DIRECTORY);
    nfuncs = (DWORD *)p; p += (NF + NSHIMS) * 4;
    nnames = (DWORD *)p; p += (NN + NSHIMS) * 4;
    nords  = (WORD  *)p; p += (NN + NSHIMS) * 2;
    if (p - region < 0) return -5;
    p = region + ((p - region + 15) & ~15);

    logmsg("ext: layout p", NULL, (unsigned long long)(ULONG_PTR)p);
    // copy original arrays
    for (i = 0; i < NF; i++) nfuncs[i] = funcs[i];
    logmsg("ext: funcs copied", NULL, 0);

    // new function entries: jump stubs appended after strings; patch later
    for (k = 0; k < NSHIMS; k++) nfuncs[NF + k] = 0;

    // merge-sorted name list: orig names + 4 shim names
    // (GetProcAddress binary-searches, so strict ascending order is required)
    {
        DWORD total = NN + NSHIMS;
        DWORD idx = 0;
        // insertion sort of shim names by strcmp
        for (k = 0; k < NSHIMS; k++) order[k] = (int)k;
        for (i = 1; i < NSHIMS; i++) {
            int t = order[i], j = (int)i - 1;
            while (j >= 0 && scmp(g_shims[order[j]].name, g_shims[t].name) > 0) { order[j + 1] = order[j]; j--; }
            order[j + 1] = t;
        }
        logmsg("ext: sort done", NULL, 0);
        {
            DWORD oi = 0; int si = 0;
            for (idx = 0; idx < total; idx++) {
                logmsg("ext: merge idx", NULL, idx);
                int take_shim = 0;
                if (si < NSHIMS) {
                    if (oi >= NN) take_shim = 1;
                    else if (scmp((const char *)(base + names[oi]), g_shims[order[si]].name) > 0) take_shim = 1;
                }
                if (take_shim) {
                    // write name string only; stubs are emitted AFTER all
                    // strings so they sit beyond the export-directory range
                    // (an RVA inside that range is parsed as a forwarder)
                    DWORD nameRVA = (DWORD)(p - base);
                    const char *nm = g_shims[order[si]].name;
                    int l = slen(nm) + 1;
                    int z;
                    for (z = 0; z < l; z++) p[z] = (unsigned char)nm[z];
                    p += (l + 15) & ~15;
                    nnames[idx] = nameRVA;
                    nords[idx] = (WORD)(NF + (DWORD)order[si]);
                    si++;
                } else {
                    // original name: keep it, but if it is one of LE's
                    // stubbed native exports, schedule its function slot
                    // for redirection to our implementation
                    const char *onm = (const char *)(base + names[oi]);
                    int q;
                    for (q = 0; q < NREDIR; q++) {
                        if (scmp(onm, g_redir[q].name) == 0) {
                            redir_ord[nredir_found] = ords[oi];
                            redir_idx[nredir_found] = q;
                            nredir_found++;
                            logmsg("ext: redir hit", NULL, (unsigned long long)q);
                            break;
                        }
                    }
                    nnames[idx] = names[oi];
                    nords[idx] = ords[oi];
                    oi++;
                }
            }
        }
    }

    // emit jump stubs after all strings, outside the directory range
    {
        DWORD dirEndRVA = (DWORD)(p - base);
        for (k = 0; k < NSHIMS; k++) {
            DWORD stubRVA = (DWORD)(p - base);
            p[0] = 0xFF; p[1] = 0x25; p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0;
            *(void **)(p + 6) = g_shims[k].impl;
            p += 16;
            nfuncs[NF + k] = stubRVA;
        }
        // redirect stubs: overwrite the existing function slot's RVA
        for (k = 0; k < (DWORD)nredir_found; k++) {
            DWORD stubRVA = (DWORD)(p - base);
            p[0] = 0xFF; p[1] = 0x25; p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0;
            *(void **)(p + 6) = g_redir[redir_idx[k]].impl;
            p += 16;
            nfuncs[redir_ord[k]] = stubRVA;
        }
        // directory size ends before stubs so stub RVAs read as code
        nt_dirSize = dirEndRVA - ((DWORD)(region - base));
    }
    logmsg("ext: merge done", NULL, (unsigned long long)nredir_found);
    // new export directory (copy, then patch counts/arrays)
    {
        DWORD *dd = (DWORD *)ned;
        const DWORD *ss = (const DWORD *)ed;
        for (i = 0; i < sizeof(IMAGE_EXPORT_DIRECTORY) / sizeof(DWORD); i++) dd[i] = ss[i];
    }
    ned->NumberOfFunctions = NF + NSHIMS;
    ned->NumberOfNames = NN + NSHIMS;
    ned->AddressOfFunctions = (DWORD)((unsigned char *)nfuncs - base);
    ned->AddressOfNames = (DWORD)((unsigned char *)nnames - base);
    ned->AddressOfNameOrdinals = (DWORD)((unsigned char *)nords - base);
    (void)BASE;

    // atomically repoint the export data directory
    logmsg("ext: before header patch", NULL, 0);
    if (!VirtualProtect(base, 0x1000, PAGE_READWRITE, &oldprot)) { logmsg("ext: vp fail", NULL, GetLastError()); return -6; }
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress = (DWORD)(region - base);
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size = nt_dirSize;
    VirtualProtect(base, 0x1000, oldprot, &oldprot);

    logmsg("EAT extended", NULL, (unsigned long long)(ULONG_PTR)region);
    return 0;
}

static void fill_basepath(HMODULE dxgi) {
    wchar_t path[MAX_PATH];
    wchar_t *s;
    int n = 0, i;
    GetModuleFileNameW(dxgi, path, MAX_PATH);
    s = wrchr(path, L'\\');
    if (s) s[1] = 0; // keep trailing backslash
    while (path[n] && n < 510) {
        wchar_t c = path[n];
        g_basepath[n] = (c < 128) ? (char)c : '?';
        n++;
    }
    g_basepath[n] = 0;
    g_basepath_len = n;
    // ReShade.ini next to the ReShade host module (= game dir)
    for (i = 0; i <= n; i++) g_inipath[i] = g_basepath[i];
    { static const char tail[] = "ReShade.ini"; int j = 0; while (tail[j]) g_inipath[n++] = tail[j++]; g_inipath[n] = 0; }
    logmsgA("ini path", g_inipath, 0);
}

static DWORD WINAPI loader_thread(LPVOID param) {
    HINSTANCE self = (HINSTANCE)param;
    wchar_t dir[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t *s;
    int i, j;
    static const wchar_t *addons[2] = { L"dlss5-bridge.addon64", L"renodx-dlss5.addon64" };

    for (i = 0; i < 120 && !is_exiting() && !GetModuleHandleW(L"dxgi.dll"); i++) Sleep(500);
    if (is_exiting()) return 0;
    {
        HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
        logmsg("dxgi.dll wait done", NULL, (unsigned long long)(ULONG_PTR)dxgi);
        if (dxgi) {
            HANDLE wth;
            fill_basepath(dxgi);
            logmsg("basepath", NULL, (unsigned long long)g_basepath_len);
            __try {
                i = extend_dxgi_exports(dxgi);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                logmsg("ext: EXCEPTION", NULL, (unsigned long long)GetExceptionCode());
                i = -100;
            }
            logmsg("extend_dxgi_exports rc", NULL, (unsigned long long)(long long)i);
            if (i == 0) {
                InitializeCriticalSection(&g_evlk);
                g_evlk_ready = 1;
                wth = CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL);
                if (wth) CloseHandle(wth);
                else logmsg("watchdog CreateThread failed", NULL, (unsigned long long)GetLastError());
            }
        }
    }

    GetModuleFileNameW(self, dir, MAX_PATH);
    s = wrchr(dir, L'\\');
    if (s) *s = 0;

    for (i = 0; i < 2; i++) {
        HMODULE m;
        if (is_exiting()) return 0;
        // renodx v4.1.5 hooks NGX only in its DllMain boot scan, while the
        // bridge loads _nvngx.dll asynchronously ~4.5s after attach; without
        // this gate the scan runs first and renodx never hooks anything.
        if (i == 1) {
            for (j = 0; j < 60 && !is_exiting() && !GetModuleHandleW(L"_nvngx.dll"); j++) Sleep(500);
            if (is_exiting()) return 0;
            logmsg("_nvngx.dll wait done", NULL, (unsigned long long)(ULONG_PTR)GetModuleHandleW(L"_nvngx.dll"));
        }
        wcpy(path, dir);
        wcat(path, L"\\");
        wcat(path, addons[i]);
        m = LoadLibraryW(path);
        if (m) logmsg("loaded", addons[i], (unsigned long long)(ULONG_PTR)m);
        else   logmsg("FAILED", addons[i], (unsigned long long)GetLastError());
    }

    {
        HANDLE cth = CreateThread(NULL, 0, cfgwatch_thread, NULL, 0, NULL);
        HANDLE pth;
        if (is_exiting()) return 0;
        if (cth) CloseHandle(cth);
        else logmsg("cfgwatch CreateThread failed", NULL, (unsigned long long)GetLastError());
        pth = CreateThread(NULL, 0, panel_thread, NULL, 0, NULL);
        if (pth) CloseHandle(pth);
        else logmsg("panel CreateThread failed", NULL, (unsigned long long)GetLastError());
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved);

BOOL WINAPI DllMainCRTStartup(HINSTANCE h, DWORD reason, LPVOID reserved) {
    return DllMain(h, reason, reserved);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH) {
        mark_exiting(); // flag only; loader lock held, do no blocking cleanup
        g_evlk_ready = 0;
        g_dispatch = NULL;
        return TRUE;
    }
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE th;
        (void)reserved;
        DisableThreadLibraryCalls(h);
        logmsg("proxy dinput8.dll attached", NULL, 0);
        th = CreateThread(NULL, 0, loader_thread, h, 0, NULL);
        if (th) CloseHandle(th);
        else logmsg("CreateThread failed", NULL, (unsigned long long)GetLastError());
    }
    return TRUE;
}
