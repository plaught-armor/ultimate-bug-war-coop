/* oodec.exe GAME_EXE JOBS OUT
 * JOBS: repeated records {u32 comp_size, u32 raw_size, comp bytes}; raw_size==comp_size means stored.
 * OUT: concatenated raw output. Calls the game's statically linked OodleLZ_Decompress. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long long (*DecompFn)(const void*, long long, void*, long long, int, int, int,
                              void*, long long, void*, void*, void*, long long, int);
#define OODLE_RVA 0x068B2B60u

static void relocate(HMODULE m) {
    char* base = (char*)m;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    long long delta = (long long)(ULONG_PTR)base - (long long)nt->OptionalHeader.ImageBase;
    fprintf(stderr, "base %p hdr %llx delta %llx\n", base, nt->OptionalHeader.ImageBase, delta);
    if (!delta) return;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    char* p = base + dir.VirtualAddress; char* end = p + dir.Size; long n = 0;
    while (p < end) {
        IMAGE_BASE_RELOCATION* br = (IMAGE_BASE_RELOCATION*)p;
        if (!br->SizeOfBlock) break;
        WORD* e = (WORD*)(br + 1); int cnt = (br->SizeOfBlock - sizeof *br) / 2;
        for (int i = 0; i < cnt; i++) {
            if ((e[i] >> 12) != IMAGE_REL_BASED_DIR64) continue;
            ULONGLONG* t = (ULONGLONG*)(base + br->VirtualAddress + (e[i] & 0xFFF));
            DWORD old; VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &old);
            *t += delta; VirtualProtect(t, 8, old, &old); n++;
        }
        p += br->SizeOfBlock;
    }
    { DWORD o; VirtualProtect(&nt->OptionalHeader.ImageBase, 8, PAGE_READWRITE, &o); nt->OptionalHeader.ImageBase = (ULONGLONG)(ULONG_PTR)base; VirtualProtect(&nt->OptionalHeader.ImageBase, 8, o, &o); }
    fprintf(stderr, "relocated %ld\n", n);
}

static void resolve_imports(HMODULE m) {
    char* base = (char*)m;
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int ok = 0, miss = 0;
    for (IMAGE_IMPORT_DESCRIPTOR* id = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); id->Name; id++) {
        HMODULE dll = LoadLibraryExA(base + id->Name, NULL, DONT_RESOLVE_DLL_REFERENCES);
        IMAGE_THUNK_DATA64* names = (IMAGE_THUNK_DATA64*)(base + (id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk));
        IMAGE_THUNK_DATA64* iat = (IMAGE_THUNK_DATA64*)(base + id->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            FARPROC f = NULL;
            if (dll) {
                if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
                    f = GetProcAddress(dll, (LPCSTR)IMAGE_ORDINAL64(names->u1.Ordinal));
                else
                    f = GetProcAddress(dll, ((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name);
            }
            DWORD old;
            VirtualProtect(&iat->u1.Function, 8, PAGE_READWRITE, &old);
            iat->u1.Function = (ULONGLONG)f;
            VirtualProtect(&iat->u1.Function, 8, old, &old);
            if (f) ok++; else miss++;
        }
    }
    fprintf(stderr, "imports resolved %d missing %d\n", ok, miss);
}

int main(int argc, char** argv) {
    if (argc != 4) { fprintf(stderr, "usage\n"); return 2; }
    HMODULE m = LoadLibraryExA(argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!m) { fprintf(stderr, "load failed %lu\n", GetLastError()); return 3; }
    setvbuf(stderr, NULL, _IONBF, 0);
    relocate(m);
    resolve_imports(m);
    DecompFn fn = (DecompFn)((char*)m + OODLE_RVA);
    FILE* in = fopen(argv[2], "rb");
    FILE* out = fopen(argv[3], "wb");
    if (!in || !out) { fprintf(stderr, "open failed\n"); return 4; }
    static unsigned char cbuf[1 << 20], rbuf[1 << 20];
    unsigned int hdr[2];
    long n = 0;
    while (fread(hdr, 4, 2, in) == 2) {
        if (hdr[0] > sizeof cbuf || hdr[1] > sizeof rbuf) { fprintf(stderr, "too big\n"); return 5; }
        if (fread(cbuf, 1, hdr[0], in) != hdr[0]) { fprintf(stderr, "short\n"); return 6; }
        if (hdr[0] == hdr[1]) { fwrite(cbuf, 1, hdr[1], out); n++; continue; }
        long long r = fn(cbuf, hdr[0], rbuf, hdr[1], 1, 0, 0, NULL, 0, NULL, NULL, NULL, 0, 3);
        if (r != (long long)hdr[1]) { fprintf(stderr, "decomp fail block %ld r=%lld\n", n, r); return 7; }
        fwrite(rbuf, 1, hdr[1], out); n++;
    }
    fclose(in); fclose(out);
    fprintf(stderr, "ok %ld blocks\n", n);
    return 0;
}
