# Card 153: The host panel shows the external address and opens the port by UPnP

Status: queued 2026-11-08 (user request), after the playtest. Budget: three runs. Play feature, host only.

Read: card 052 (co-op panel, `ubw::lobby::local_ipv4`), card 045 (listen URL and port), docs/PLAYTEST.md "Play".

## Why

Pressing H shows only the LAN addresses from `GetAdaptersAddresses` (mod/src/lobby.cpp:191,
mod/src/ubw/lobby_ui.cpp:688). A friend over the internet needs the host's external address and a forwarded UDP
port. Behind NAT the PC cannot see its external address, so today the host looks it up on a website and forwards
the port by hand. The panel also shows no port, although the host may listen on a port other than 7777 (`-port=N`,
tools/playtest.sh `PORT`; the user's play launches have used 47000).

## Do

1. UPnP, at the H press, on a worker thread (never the game thread):
   - Find the router's IGD (SSDP M-SEARCH for `urn:schemas-upnp-org:service:WANIPConnection:1`, then
     `WANPPPConnection:1`) and read its control URL from the device description.
   - Call `GetExternalIPAddress`.
   - Call `AddPortMapping` for UDP on the listen port to this PC's LAN address, with the description "UBW Co-op"
     and a lease of 0 or 4 h. Log the result code.
   - Remove the mapping (`DeletePortMapping`) when hosting is turned off and at game exit. Only remove a mapping
     this mod added.
   - Prefer the OS's UPnP NAT API (`IUPnPNAT` / `IStaticPortMappingCollection` in natupnp.dll, COM) if it works
     under Proton. Else use a small SSDP + SOAP client over Winsock and WinHTTP. Decide statically first, and write
     down which one and why.
2. Fallback for when UPnP gives no external address: an HTTPS GET to `https://api.ipify.org` (WinHTTP, 3 s timeout,
   plain-text IPv4, validated as a dotted quad before it is shown). Show it as "external (port not opened
   automatically)".
3. The panel's host lines show:
   - the external address with the real listen port (`a.b.c.d:port`);
   - the port-mapping state: opened by UPnP, UPnP not available (forward UDP `port` by hand), or failed with its
     code;
   - the LAN addresses as today.
   The text is "looking up..." until the worker answers. A missing answer must not block the panel or the
   mission start.
4. The listen port shown and mapped is the one the host really listens on (read from the `-port` command line or
   the engine's listen URL, card 045), not `kDefaultPort`.

## Out of scope

NAT punch-through, relay servers, IPv6, and the client side. The joiner types `a.b.c.d:port` as today.

## Done when

- Run on this machine with the router: the H press logs the external address, the AddPortMapping result, and the
  panel text, and the external address matches the ipify answer. Turning hosting off logs DeletePortMapping.
- If the router has no UPnP, the run shows the fallback path instead, and the card says so.
- A join test from outside the LAN is done by the user at the next playtest; it is not part of this card's runs.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Senior prep (2026-11-12, static)

- The Proton prefix has no `natupnp.dll`; `hnetcfg.dll`, `winhttp.dll` and `iphlpapi.dll` are present (Wine
  builtins). Whether Wine's `hnetcfg` answers `CLSID_UPnPNAT` with a working port mapping depends on how that
  Proton build was compiled, and a friend may host on Windows. Default decision for Do 1: the small SSDP + SOAP
  client over Winsock (one code path on both systems, no COM); the junior overrides it only with a written reason.
- Existing patterns to reuse: `lobby.cpp:193` resolves `iphlpapi.dll` by `LoadLibraryW` + `GetProcAddress` (the
  proxy keeps kernel32 as its only import; do the same for `ws2_32.dll` and `winhttp.dll`), and
  `movieskip.cpp:117` is the worker-thread pattern. The worker owns fixed buffers, hands the game thread a result
  through one atomic state, and never touches game objects.
- The run adds a UDP port mapping on the user's router and one HTTPS request to api.ipify.org: both are the
  card's stated behaviour, on the H press only. The run must log the DeletePortMapping at exit so no mapping is
  left behind; check the router's answer in the log before the card closes.

## Senior prep addendum (2026-11-13, static)

- Port source for Do 4: `lobby.cpp:127` starts from `kDefaultPort` (`lobby.h:13`, 7777) when it parses a join
  address; no code reads the host's own listen port today. The host launch passes `-port=N` on the command line
  (`tools/run_pair.sh:73`, `tools/playtest.sh` `PORT`). Read it once with `GetCommandLineW` (case-insensitive
  `-port=`), validate 1024 to 65535, fall back to `kDefaultPort`; log the value at the H press. Do not read
  engine memory for it.
- Test launches must not map a port on the router: gate the UPnP worker off when `UBWCOOP_TRAVEL` or the test
  driver is active, and prove the card with one manual host-only launch.
- Files: `mod/src/lobby.cpp`, `mod/src/lobby.h`, `mod/src/ubw/lobby_ui.cpp` (panel lines at `:688`), one new file
  `mod/src/upnp.cpp` for the worker. Priority: after the playtest.

## Senior review of the build 5 diff (2026-11-13, static): one required fix, then run

Attacked, no defect found: worker hand-off (`launch` / `worker` compare-exchange on `g_busy`: one worker at a
time, no lost request in either order of start/stop against worker exit); buffer bounds in `recv_all`,
`copy_until`, `parse_url`, `parse_ipv4`, SSDP reply (1023 + NUL), SOAP body and request (snprintf length
checked); socket and WinHTTP handles closed on every path; ipify answer accepted only as a whole dotted quad of
at most 15 chars; test gate `c153_gated` (`lobby_ui.cpp:683`) stops `start` and `stop` under `UBWCOOP_TRAVEL` /
`UBWCOOP_CMD`; no game object touched off the game thread.

Required before the run:

1. `http_call` uses a blocking `connect`; `SO_SNDTIMEO` does not bound it on Windows (about 21 s when the router
   does not answer). `exit_cleanup` runs it inside `DllMain` `DLL_PROCESS_DETACH` (`dllmain.cpp:77`), so a host
   whose network dropped hangs at exit under the loader lock. Bound the connect by `wait_ms`: non-blocking
   socket (`ioctlsocket` FIONBIO), `connect`, `select` for write with the timeout, then back to blocking; load
   `ioctlsocket` and `select` with the other ws2_32 functions. Log the timeout as a WARN.

Small, same pass:

2. `poll` shows the previous result for the frames between the worker taking `g_want_on` and `job_on` publishing
   Looking, and for the whole of a `stop` (old Mapped until `job_off` publishes). Make `poll` return Looking
   while `g_busy` is set, not only while `g_want_on` is.
3. A relative `controlURL` is copied with stop set `<` only (`upnp.cpp:331`); use the same stop set as
   `parse_url` (`" \t\r\n\"<"`) after skipping leading white space, so no CR/LF reaches the request line.
4. `external_from_web` reads `GetLastError` after the read loop; take it at the failing call.

Accepted as is: SSDP LOCATION from any LAN device is followed when it is an `http://a.b.c.d` literal (friends
and home LAN trust model); the seqlock read of `g_result`; network calls at process detach (kept, the 4 h lease
is the backstop) once item 1 bounds them.

Run (after the playtest, one manual host-only launch): as the card's prediction, plus exit within 2 s of the
quit with a `DeletePortMapping ... HTTP 200` row.

Review fixes 1 to 4 done by the junior and re-read by the senior 2026-11-13 (`connect_bounded`, `upnp.cpp:301`;
`poll` Looking while busy; controlURL stop set; error taken at the failing call). Accepted. Compile hash
`faaa085d7714170b`. Run owed: one manual host-only launch after the playtest.

## Closed (2026-11-18 21:03, senior, b253 `168fd79b995e1d9c`, c153b253c)

Evidence (junior, manual host-only launch, logs/c153b253c-host.log): "router lookup started", "GetExternalIPAddress:
HTTP 200", "AddPortMapping UDP 7778 ... lease 14400 s: HTTP 200, UPnP error 0"; alt+F4 at the menu, "UBWCoop
detach", "DeletePortMapping UDP 7778: HTTP 200, UPnP error 0" 56 ms later, process gone 1.19 s after the key.
Not done: a quit during boot waits until the menu loads (15.87 s measured), engine behaviour, mapping still removed.
Reopen when: a host's mapping or external address fails in play.
