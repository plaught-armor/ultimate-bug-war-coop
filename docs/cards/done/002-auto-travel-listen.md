# Card 002: Auto-travel (host listen / client connect) from env vars, plus travel logging

Read `docs/STATE.md` first, then the senior review at the end of `docs/cards/001-toolchain-proxy-dll.md`. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why
Milestone 1 needs two game instances in the same map. The game ignores the map URL on the command line and always boots to the main menu, so our DLL has to start the travel itself. Each instance is told what to do through environment variables, which pass through umu-run/Proton into the Windows process. Both instances run from the same exe directory, so per-instance settings cannot live in a shared file next to the exe.

## Already done (do not redo)
- The proxy, MinHook, the log, `pdb_rva.py` and the `UGameEngine::Init` proof hook all work at runtime (card 001).
- These publics exist in the PDB (exact decorated names):
  - `?Tick@UGameEngine@@UEAAXM_N@Z`: `void UGameEngine::Tick(float DeltaSeconds, bool bIdleMode)`, game thread, every frame.
  - `?GetGameWorld@UGameEngine@@QEAAPEAVUWorld@@XZ`: `UWorld* UGameEngine::GetGameWorld()`.
  - `?SetClientTravel@UEngine@@QEAAXPEAVUWorld@@PEB_WW4ETravelType@@@Z`: `void UEngine::SetClientTravel(UWorld*, const TCHAR* NextURL, ETravelType)`. TCHAR is `wchar_t`. `TRAVEL_Absolute` = 0.
  - `?Browse@UEngine@@UEAA?AW4Type@EBrowseReturnVal@@AEAUFWorldContext@@UFURL@@AEAVFString@@@Z`: `EBrowseReturnVal::Type UEngine::Browse(FWorldContext&, FURL, FString& Error)`. Success = 0, Failure = 1, Pending = 2.
  - `?LoadMap@UEngine@@UEAA_NAEAUFWorldContext@@UFURL@@PEAVUPendingNetGame@@AEAVFString@@@Z`: `bool UEngine::LoadMap(FWorldContext&, FURL, UPendingNetGame*, FString& Error)`.
  - `?Listen@UWorld@@QEAA_NAEAUFURL@@@Z`: `bool UWorld::Listen(FURL&)`.
  - Under the MSVC x64 ABI, `FURL` passed by value arrives as a pointer to a caller-owned copy.

## Do
1. **Fold-ins from the card 001 review.** Log the exe's PE `TimeDateStamp`. Log an explicit line when the build-identity check passes, not only when it fails.
2. **Per-instance log name.** If `UBWCOOP_LOG_TAG` is set (validate it: 1–16 chars of `[A-Za-z0-9_-]`, otherwise ignore it and log a warning), the log file is `UBWCoop-<tag>.log`. Otherwise it stays `UBWCoop.log`. Use `GetEnvironmentVariableW` and check its return value (0, or truncated).
3. **Config from env** (read once in the worker thread, then log the values):
   - `UBWCOOP_TRAVEL`: the URL passed to `SetClientTravel`, e.g. `/Game/Maps/FED/FED_C1M1?listen` (host) or `127.0.0.1:7777` (client). If unset, the mod does no travel and logs that.
   - `UBWCOOP_TRAVEL_DELAY_MS`: delay after the first `Tick` with a non-null game world before the travel fires. Default 5000, clamp to [0, 600000], and log any parse failure.
   - Bound the URL length (for example 512 wchar_t). If it is longer, log and disable travel.
4. **Hooks** (add the symbols to `tools/symbols.txt`, regenerate with `pdb_rva.py gen`, install through the same checked path as card 001):
   - `UGameEngine::Tick`: call the original first. Then, at most once per process: once the game world is non-null and the delay has passed (measure with `GetTickCount64`), call `SetClientTravel(world, url, 0)` on the game thread and log it. Tick-path cost when idle must be a couple of branches, with no logging every frame.
   - `UEngine::Browse`: log entry, then the return value and, when it is non-empty, the `Error` string.
   - `UEngine::LoadMap`: log entry, then the bool result and any `Error`. Also log the URL's Host, Port and Map **only if** you have verified the `FURL` and `FString` layouts from the PDB's type records (`llvm-pdbutil dump --types` or `pretty -classes`). Put the field offsets you found in the report. If you can't verify them, skip URL logging and say so.
   - `UWorld::Listen`: log the bool result.
   - Reading an `FString` (`{wchar_t* Data; int32 Num; int32 Max}`, where Num includes the NUL): check that Data is non-null and that `0 < Num <= 1024`, then log it with `%.*ls`. Put this in one helper.
5. Keep `/W4 /WX` clean. Each new file or function stays small (≤60 lines per function). Check every return value and log every failure, as in card 001.
6. Build it and show proof: a clean build log, `llvm-readobj --coff-exports` still identical to the GE-Proton dwmapi (84), imports still only KERNEL32.dll, the new RVAs from `pdb_rva.py gen`, and a prologue disassembly of each new hook target (MinHook needs at least 5 relocatable bytes, so flag anything odd, such as a jmp thunk).

## Constraints
- Do not launch the game, and do not touch `game/`, `prefix/` or `logs/`. The senior runs the two-instance runtime test.
- No subagents. No commits.

## Report
**Status: built, not runtime-tested (per card). Ready for the two-instance test.**

### Files
- New `mod/src/config.h`, `mod/src/config.cpp`: env reading. One `read_env` helper around `GetEnvironmentVariableW` that separates Unset (`ERROR_ENVVAR_NOT_FOUND`), Empty, TooLong (return >= cap), Error (other `GetLastError`) and Ok. `read_log_tag()` (no logging, runs under the loader lock), `read_travel()` (logs the values or the failure).
- `mod/src/log.{h,cpp}`: `open_next_to_exe(const wchar_t* tag)` builds `UBWCoop.log` or `UBWCoop-<tag>.log` with a bounded `append` helper.
- `mod/src/dllmain.cpp`: reads the tag before opening the log, then logs `attach: log tag "<tag>"` or `WARN attach: UBWCOOP_LOG_TAG ignored ...`. The worker calls `read_travel()` and then `hooks::install(travel)`.
- `mod/src/hooks.{h,cpp}`: TimeDateStamp logged, plus an explicit `hooks: build identity OK` line. All targets and called functions are resolved through `code_address` before anything is patched. Hooks come from a table installed in order Init, Browse, LoadMap, Listen, **Tick last**, so travel can only fire once every logging hook is in. `log_fstring` helper. Detours for Tick, Browse, LoadMap, Listen.
- `tools/symbols.txt`: 6 new entries. `mod/src/offsets.generated.h` regenerated. `mod/CMakeLists.txt`: added `src/config.cpp`.

### Behaviour
- `UBWCOOP_LOG_TAG`: 1-16 chars of `[A-Za-z0-9_-]`. Too long, empty, a bad character or a read error → ignored with a WARN, and the log stays `UBWCoop.log`.
- `UBWCOOP_TRAVEL`: buffer is 512 wchar_t + NUL. Longer → `WARN ... needs N chars incl. NUL, limit 513 -- no auto-travel`. Unset, empty or error → logged, no travel.
- `UBWCOOP_TRAVEL_DELAY_MS`: digits only, at most 10, must fit in u32. Otherwise WARN and default 5000. Above 600000 → WARN and clamp. There is nothing to clamp below 0, because a minus sign is a parse failure (logged).
- Tick detour: calls the original, then `if (g_travel_armed) travel_step(this)`. When idle or after firing, that is one load and one branch, with no per-frame logging. `travel_step`: `GetGameWorld(this)`. Null → return. On the first non-null world it stores the `GetTickCount64` time and logs `travel: first game world %p, travelling in N ms`. Once `now - seen >= delay`, it clears armed (before the call, so it fires once even if reentered) and calls `SetClientTravel(this, world, url, 0)` on the game thread, logging before and after. With a delay of 0, travel fires on the tick after the first world is seen.
- The travel state is written by the worker before the Tick hook is enabled. MinHook's suspend/resume of the other threads orders those writes. After that, only the game thread touches the state.
- Browse: `UEngine::Browse enter (ctx=%p)`, then `returned %d (Success|Failure|Pending)`, then Error if non-empty. LoadMap: enter (ctx, pending), `returned true|false`, Error. Listen: `UWorld::Listen(world=%p) returned true|false`.
- `log_fstring`: null `FString*` → logged. `Num==0`, or `Num==1` with non-null Data → treated as empty and not logged. Data null, Num<0 or Num>1024 → `<unreadable FString data=.. num=..>`. Otherwise `%.*ls` with `Num-1`.

### FURL / FString layout: NOT verified, URL logging skipped
The PDB is **stripped of types**. `llvm-pdbutil dump --summary` says `Is stripped: true`, the TPI and IPI streams are 56 bytes each, and `dump --types` / `--ids` show 0 records. `pretty` needs DIA, which this LLVM build lacks. So there are no type records for FURL or FString. Per the card, **Host/Port/Map logging is skipped.** The detours take FURL as an opaque `void*` (pointer to the caller's copy) and pass it through untouched. The original keeps destroying it, as the MSVC x64 ABI requires.
FString uses the stock UE `TArray<TCHAR>` shape `{wchar_t* @0, int32 Num @8, int32 Max @12}`. This is also unverified from the PDB. Indirect evidence from the exe: `GetGameWorld` and `SetClientTravel` both read `UEngine::WorldList` as a pointer at `this+0x1148` and an int32 count at `this+0x1150` (`movslq 0x1150(%rcx)`), the same TArray shape. The helper's bounds checks cap the damage if the shape were wrong. Senior: decide whether that evidence is enough, or whether Error logging should wait. One option for the FURL layout later: disassemble a known FURL consumer such as `FURL::ToString`, if it exists in the publics.

### Build
```
python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && \
cmake -G Ninja -S mod -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build
```
Clean rebuild in a fresh dir: 11/11 steps, 0 lines matching warn|error (`/W4 /WX`). `build/dwmapi.dll` is 146432 bytes. Two builds differ only at byte 129 (the link TimeDateStamp; no `/Brepro`).

### Proof
- Exports: `llvm-readobj --coff-exports` ordinal+name list is identical to `GE-Proton11-11-x86_64/files/lib/wine/x86_64-windows/dwmapi.dll` (84 = 84, `diff` empty).
- Imports: only `KERNEL32.dll`.
- RVAs (`pdb_rva.py gen`, identity check passed):
```
0x03C19AD0  UGameEngine_Init
0x03C25B10  UGameEngine_Tick
0x03C12EE0  UGameEngine_GetGameWorld   (called, not hooked)
0x043ECE40  UEngine_SetClientTravel    (called, not hooked)
0x043D4840  UEngine_Browse
0x043E6270  UEngine_LoadMap
0x04347540  UWorld_Listen
```
- Prologues (ImageBase 0x140000000). Every hooked target is a real function body, not a jmp thunk, with at least 5 relocatable bytes:
  - Tick `143c25b10`: `40 55 push rbp; 53 push rbx; 41 54 push r12; 41 55; 41 57; lea rbp,[rsp-0x20]; sub rsp,0x120; movaps [rsp+0x100],xmm6`
  - Browse `1443d4840`: `40 55; 53; 56; 57; 41 54; 41 56; lea rbp,[rsp-0x198]; sub rsp,0x298; mov rax,[rip+0x4d47421]`. The RIP-relative load is at +0x18, past MinHook's 5-byte patch window, and MinHook relocates RIP-relative operands anyway.
  - LoadMap `1443e6270`: `48 8b c4 mov rax,rsp; 4c 89 48 20 mov [rax+0x20],r9; mov [rax+0x18],r8; mov [rax+8],rcx; push rbp...`
  - Listen `144347540`: `48 89 5c 24 10 mov [rsp+0x10],rbx; mov [rsp+0x18],rbp; mov [rsp+0x20],rsi; push rdi; sub rsp,0x50; cmp qword [rcx+0x38],0`
  - Called, not hooked: GetGameWorld `143c12ee0` `mov r9,rcx; xor eax,eax; ...; cmp eax,[r9+0x1150]; mov rcx,[r9+0x1148]` (WorldList walk). SetClientTravel `1443ece40` `mov [rsp+8],rbx; mov [rsp+0x10],rsi; push rdi; sub rsp,0x20; mov rax,[rcx+0x1148]; mov edi,r9d`. `r9d` = the 4th argument (ETravelType), which confirms the `(this, world, url, type)` register mapping.

### Uncertainties / for senior
1. **Exe TimeDateStamp is `0x41674F8A` (2004-10-09).** That is a fake or deterministic value, not the build date, so it is useless as an identity check. The RSDS GUID/age check is still the real gate. Logged anyway, as asked.
2. FString layout is not PDB-verified (see above). URL fields are not logged.
3. `%ls` goes through the static CRT's `vsnprintf` in the "C" locale. A non-ASCII char in the URL, tag or Error makes that whole line `<format error>`. Tags are ASCII by validation. URLs and errors in practice are too.
4. If a later hook in the table fails, the earlier ones stay enabled. That is the same behaviour as card 001. Travel is never armed without the Tick hook, and Tick goes last.
5. `GetGameWorld` returns the frontend world at the main menu, so the delay counts from the first menu tick. If the menu needs dismissing ("Authentication failed" / "controller disconnected") before travel works, raise `UBWCOOP_TRAVEL_DELAY_MS`. Untested.
6. Host URL suggestion: `/Game/Maps/FED/FED_C1M1?listen`. Client: `127.0.0.1:7777` (default port, not checked in this build).

### Senior review (2026-11-02)
**Accepted.** Rebuilt fresh (0 warnings). Exports are identical (84) and imports are only KERNEL32. Runtime: host travel + `Listen` true, UDP 7777 bound; the client connected (Browse Pending, then LoadMap with a pending net game, true). The host then crashed in the game's own code (remote-PC LocalPlayer assumption, see STATE), not in our hooks. FString logging is accepted on the bounds-checked assumption. Follow-up: card 003.
