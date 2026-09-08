#include <windows.h>
__declspec(dllexport) unsigned long long ReShadeRegisterAddon(void *a, unsigned int v) { (void)a; (void)v; return 1; }
__declspec(dllexport) void ReShadeUnregisterAddon(void *a) { (void)a; }
__declspec(dllexport) unsigned long long ReShadeRegisterEvent(int ev, void *cb) { (void)ev; (void)cb; return 1; }
__declspec(dllexport) void ReShadeUnregisterEvent(int ev, void *cb) { (void)ev; (void)cb; }
__declspec(dllexport) void ReShadeLogMessage(void *a, int lvl, const char *msg) { (void)a; (void)lvl; (void)msg; }
__declspec(dllexport) unsigned long long ReShadeGetConfigValue(void *a, const char *s, const char *k, void *v) { (void)a; (void)s; (void)k; (void)v; return 0; }
__declspec(dllexport) unsigned long long ReShadeSetConfigValue(void *a, const char *s, const char *k, const char *v) { (void)a; (void)s; (void)k; (void)v; return 0; }
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID x) { (void)h;(void)r;(void)x; return TRUE; }
BOOL WINAPI DllMainCRTStartup(HINSTANCE h, DWORD r, LPVOID x) { return DllMain(h, r, x); }
