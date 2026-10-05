# INV-01: gate run G0 / G1 / G2 (READ-ONLY)

Run by: Experimental Validation Specialist. Date: 2026-09-30, 12:04:44 to 12:05:04 BRT (America/Recife, UTC-3).
Machine: cachyos-x8664 (`79dac8ac-9d7c-4ae5-8caa-47c725ac2b3f`), Linux, game under Proton Experimental.
Scope: G0, G1, G2 only. **G3 and everything from Phase 1b onward were NOT run.**
What was done to the game: nothing. There were no memory writes, no ptrace attach, no injection or hooks, no signals, and no network or firewall changes. Memory reads used only `open("/proc/4113076/mem","rb")` (read-only fd, seek+read), which the user approved through the per-command approval card.
Commands: 2 approved invocations on the user machine.
- Run A: the full probe. Script: `/workspace/validation/gates/g012_readonly.sh`. The middle of its output was truncated by the tool, because `pgrep -af` echoed the wrapper shell's own long command line.
- Run B: a short re-read of the truncated section, plus a tick-liveness sample.

## Summary

| Gate | Verdict | Grade | Key evidence |
|---|---|---|---|
| G0 build identity | **PASS** (G0.1-G0.4). The full in-memory `.text` hash differs from disk; the difference is explained below. | CONFIRMED (disk SHA-256, mapped inode, PE timestamp, 12/12 code windows) | SHA-256 `c119f509...5d73442`, 24,668,160 B; maps inode 42601200 at 0x140000000; TimeDateStamp 0x605cbae3 in both disk and memory; 12/12 windows MATCH. `.text` mem != disk in exactly 5 runs x 5 B (`e9` jmp detours, attributed PROBABLE to DR2Hook), none near the physics code. |
| G1 offline | **PASS, using the substitute criteria the user and the RE Orchestrator accepted.** The plan's strict G1.1 (network namespace / nftables) is **NOT met**. | **PROBABLE-offline** (cap; never CONFIRMED) | 0 ESTAB/SYN_SENT TCP to non-loopback; 0 UDP sockets with a remote peer; NetworkGuard getaddrinfo/GetAddrInfoW/connect hooks installed at 11:33:58, 0 failure lines, 22 blocked external DNS lookups (latest 12:04:14); user attestation relayed 12:01 BRT; rig pointer unchanged since 11:38 |
| G2 rig sanity / confounds | **PASS, clean** (`confound_flags = 0x00`) | Valid only for this frozen snapshot; must be re-checked per trial | All 5 sanity checks true; all 5 confound fields 0 (6 samples 12:04:46-47). **The simulation is not ticking.** The state is bit-identical to MC's 11:38 frozen dump. |

---

## G1: Offline gate

### G1.1 Processes and sockets
`pgrep -af -i dirtrally2`, 12:04:44 BRT. The game and its Wine/Proton chain, all started 11:33:55-11:33:57 BRT:

| PID | Role |
|---|---|
| 4112898 | steam-runtime-launch-client |
| 4112901 | steam-runtime-supervisor |
| 4112902 | reaper |
| 4112905 | srt-bwrap |
| 4112964 | pv-adverb |
| 4112997 | python3 proton |
| 4112999 | `c:\windows\system32\steam.exe` |
| **4113076** | **`S:\steamapps\common\DiRT Rally 2.0\dirtrally2.exe -novr`**: the game process (maps dirtrally2.exe at 0x140000000) |

The match on 4156928 was the probe's own zsh wrapper, so it is excluded.
Wine children of pv-adverb 4112964 were also included in the filter: wineserver 4113001, services.exe 4113005, winedevice.exe 4113008 and 4113031, plugplay.exe 4113018, svchost.exe 4113024, explorer.exe 4113047, rpcss.exe 4113056, tabtip.exe 4113066, and xalia.exe 4113068.

**Network namespace:** every PID above is in `net:[4026531833]`, which is the same namespace as the host shell. There is **no netns / `--net=none` isolation**, so the plan's G1.1 isolation requirement is not met. The substitute evidence was used instead, as agreed.

`ss -tupn`, filtered to all PIDs above (12:05:00):
```
tcp   ESTAB 0 0  127.0.0.1:57343  127.0.0.1:60882 users:(("steam-runtime-l",pid=4112898,fd=104),("steam",pid=1255471,fd=104))
tcp   ESTAB 0 0  127.0.0.1:57202  127.0.0.1:57343 users:(("dirtrally2.exe",pid=4113076,fd=30))
```
Both are loopback. The game holds one TCP connection, to the Steam runtime launch client or Steam client IPC.

`ss -uapn`, filtered: **27 UNCONN UDP sockets**, all owned by dirtrally2.exe 4113076. All are bound to `0.0.0.0` with peer `0.0.0.0:*` (no connected peer). The ports are 47049, 39057, 55566, 47407 (also held by wineserver fd 3247), 56101, 48559, 48667, 40511, 57088, 32779, 41152, 41513, 58689, 50572, 58900, 42559, 34656, 59748, 51746, 51814, 35480, 60655, 44361, 45016, 45514, 37829, and 54406. The full raw lines are in the Run B output below.

`ss -tlpn`, filtered: no lines (no TCP listeners).

Independent cross-check without `ss`, run A: `/proc/<pid>/fd` socket inodes were matched against `/proc/<pid>/net/{tcp,tcp6,udp,udp6,raw,raw6,unix,netlink}`.
- 4113076: 39 socket fds (11 unix, 27 udp, 1 tcp); **0 non-local-peer inet sockets**.
- 4112898: 1 tcp (loopback ESTABLISHED) and 2 unix.
- 4112999: 4 unix.
- The other chain PIDs have 0 sockets.

**G1.1 check result: PASS.** There is no ESTABLISHED/SYN_SENT TCP to a non-loopback address and no UDP socket with a non-local remote peer.

Caveats (they limit the grade):
- (a) Unconnected UDP sockets on 0.0.0.0 can `sendto()` any address and can receive from the LAN or Internet. A socket-table snapshot cannot rule out datagram traffic.
- (b) There are 27 of them, which is a lot for an offline session. They are likely the game's or Wine's network layer, idle.
- (c) The game's Steam API traffic goes over loopback IPC to the Steam client, which is itself online. Anything Steam-mediated (stats, leaderboards) would leave through the Steam process, not the game's own sockets.
- (d) This is a single snapshot.

### G1.2 DR2Hook log
Found at `/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/dr2hook.log`: 14,376 B, 115 lines, mtime 2026-09-30 12:04:14 BRT. The session started 11:33:57.851, which matches the game start at 11:33:57. The DR2Hook files present are `dxgi.dll` (proxy, WINEDLLOVERRIDES dxgi=n,b) and `dr2hook_core.dll`, both 08:41 today.
```
[11:33:57.851] [INFO] DR2Hook Logger session started (v0.1.0)
[11:33:57.891] [INFO] DR2Hook Core v0.1.0 inicializando...
[11:33:57.893] [INFO] Mod carregado com sucesso: Practice Mode (dr2.practice_mode) v1.0.0
[11:33:58.389] [INFO] Hook de Present instalado e habilitado com sucesso.
[11:33:58.394] [INFO] NetworkGuard: Hook getaddrinfo instalado e habilitado.
[11:33:58.398] [INFO] NetworkGuard: Hook GetAddrInfoW instalado e habilitado.
[11:33:58.403] [INFO] NetworkGuard: Hook connect instalado e habilitado.
[11:33:58.403] [INFO] Hooks principais inicializados com sucesso.
...
[11:34:14.269] [INFO] NetworkGuard: Resolucao DNS externa (Unicode) bloqueada (Air-gap offline ativo).
...
[12:04:14.217] [INFO] NetworkGuard: Resolucao DNS externa (Unicode) bloqueada (Air-gap offline ativo).
[12:04:14.219] [INFO] NetworkGuard: Resolucao DNS externa (Unicode) bloqueada (Air-gap offline ativo).
```
- Counts: `bloqueada` (blocked) = **22** lines. Lines matching `fail|falh|erro|error|MH_ERROR|MH_E` = **0**.
- The blocked lookups come in pairs at 11:34:13-14, 11:35:10, 11:36:36, 11:38:38, 11:41:43, 11:47:15, 11:54:45, and 12:04:14. The game keeps retrying an external lookup with a growing back-off, and each retry is blocked.

**G1.2 check result: PASS.** The hooks installed, there are no failures, and there are refused external lookups.

Caveats:
- NetworkGuard hooks only `getaddrinfo`, `GetAddrInfoW`, and `connect`. There is no log evidence of hooks on `sendto`/`WSASendTo`, `WSAConnect`/`ConnectEx`, or `gethostbyname`, so the UDP egress path is **not** guarded.
- The log line does not name the host being resolved.
- Per plan N6, SafetyGuard is permissive, so this is defence in depth only.

### G1.3 Game mode
The user attests they are in a local/offline special stage. The RE Orchestrator relayed this at **12:01 BRT**. No screenshot was taken (`offline_evidence_ref` = attestation only). The plan's allowed-mode list (DirtFish free roam / offline Time Trial / Custom Championship) was not confirmed by name.

### G1.4 Session identity
Rig pointer `exe+0x1681ce8 -> car 0x4b254b80 -> +0x30 con 0x4b28f100 -> +0x08 rig 0x4b29bab0`. This is the same as MC's 11:38 dump (`dump_rig_4b29bab0.bin`) and the same PID 4113076 as MC's `build_info.txt`.

### G1 verdict
**PASS (substitute criteria). Grade: PROBABLE-offline.** It is not CONFIRMED because:
- there is no OS-level isolation;
- 27 unconnected UDP sockets exist;
- NetworkGuard does not cover sendto;
- the mode rests on attestation only.

`offline_gate_status = PASS (substitute, PROBABLE)`. The plan's own fail-closed wording would read this as INDETERMINATE for writes, because G1.1 network isolation was not set up. That decision belongs to the RE Orchestrator.

---

## G0: Build identity

| Check | Result |
|---|---|
| G0.1 disk SHA-256 | `c119f509fc315d8168aa7aaa5d0a1ee8e3570c7220bd6cbb2d337d6725d73442` (sha256sum and python agree), **MATCH** |
| G0.1 size | 24,668,160 B, **MATCH** |
| Disk file stat | inode 42601200, mtime 2026-09-07 10:25:52.474 BRT |
| G0.2 maps | `140000000-140001000 r--p 00000000 103:02 42601200 /mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/dirtrally2.exe`. Same path, same inode 42601200, **MATCH**. `/proc/4113076/exe` is `wine64-preloader`, as expected under Wine. |
| G0.3 TimeDateStamp | disk 0x605cbae3, mapped 0x605cbae3 (2021-03-25 13:31:31 BRT). Header [0:0x400] is byte-identical. **MATCH** |
| G0.4 code windows (32 B each) | 0x14074b8f0, 0x14073e0c0, 0x140746150, 0x1407395f5, 0x14073e30f, 0x14074d190, 0x14074ad80, 0x14074a910, 0x14074a890, 0x1407511e0, 0x140dbc430, 0x140dbca20: **12/12 MATCH** |
| Full `.text` (va 0x1000, 0x109d22e B) | disk `c770ba29c49fbad2f00ad64b7276dee0c2162eb92b159ae8299baba03567f5e5`; memory `86a2d4960ce41786a39aa57e3633dc8fa8fb30c171806d091fb6fc585e08f442`. **DIFF.** The memory hash equals MC's `text_hash.txt` value (~12:01), so nothing changed between the two reads. |

The `.text` diff is exactly 5 runs, 25 bytes. Each is a 5-byte `e9` jmp to a relay page at 0x13fff0000 (rwxp, anonymous), which does `ff25` to anonymous r-xp code at 0x6ffffb2d1000-0x6ffffb3c7000:
```
0x140285640 disk=48895c2408 mem=e90eb9d6ff -> 0x13fff0f53 -> 0x6ffffb2d50c0
0x1403a8820 disk=48895c2410 mem=e9ae86c4ff -> 0x13fff0ed3 -> 0x6ffffb2da6d0
0x140c54270 disk=48895c2420 mem=e95ecd39ff -> 0x13fff0fd3 -> 0x6ffffb2e2a70
0x140d040c0 disk=48895c2410 mem=e9cece2eff -> 0x13fff0f93 -> 0x6ffffb2d54c0
0x140d09380 disk=807928000f mem=e9907b2eff -> 0x13fff0f15 -> 0x6ffffb2d5130
```
These are MinHook-style detours. They are attributed to DR2Hook (UiData / PauseMenu / NativeScreen hooks in the log): **PROBABLE**. The target region is anonymous, so the owning module was not proven. None of the 5 sites is in the physics ranges (0x140730000-0x140760000, 0x140dbc000-0x140dbd000) or at any G0.4 address.

For comparison, MC's `.rdata` / `.data` diffs (IAT fix-ups and live data) are expected and are not part of G0.

**G0 verdict: PASS** for G0.1-G0.4 as the plan defines them. The in-memory `.text` hash check is complete, not pending, and it is **not** equal to disk, because of the 5 foreign detours described above. The G3 / Phase 1b harness must coexist with these and must re-verify them.

---

## G2: Rig sanity and confounds (read-only, 6 samples 12:04:46-12:04:47, plus 7 liveness samples 12:05:01-12:05:04)

| Check | Expected | Observed |
|---|---|---|
| `[rig+0x12c0] == rig` | true | true |
| `[rig+0x12d0]` | 4 | 4 |
| `con+0` | 0x141400c30 | 0x141400c30 |
| `con+8 == rig` | true | true (tautological: rig is resolved via con+8) |
| `rig+0 == con` | true | true |
| `rig+0x2550` (H-022 hook) | 0 | 0x0 |
| `rig+0x3b8` (N1 adjuster) | 0 | 0x0 |
| byte `rig+0x2548` (freeze) | 0 | 0 |
| `rig+0x2560` (kinematic) | 0 | 0x0 |
| `con+0xcc0` (placement) | 0 | 0x0 |

`confound_flags = 0x00`, so the snapshot is **clean**.

Liveness finding: the simulation is not advancing. Across 12:04:46-12:05:04 (~18 s):
- `rig+0x1338` = `f5762f42cd210242`
- `rig+0xdc` = 6
- p `rig+0x2d0` = `ab0f8dc581e10143d306d242`
- v `rig+0x2b0` = (6.4298, 0.1650, 7.9646)
- abs(v) `+0x2508` = 10.2374

All of these are constant, and all are **bit-identical to MC's 11:38 frozen dump**. The car state has not ticked for about 27 min. This is consistent with the pause screen or a frozen/menu state.

Consequences:
- The G2 flags are valid for this frozen state only.
- Phase 1a R1-R6 would record zero bursts until the user resumes driving.
- G2 must be re-run per trial once the simulation is live.

**G2 verdict: PASS (clean).**

---

## Other observations for the RE Orchestrator
1. The **Practice Mode mod is loaded** in this session: "Press F5 to save checkpoint, F6 to restore, F7 for momentum". F6 and F7 write car state from Present, which is the shipping ApplyState path (N7). The operator must not press F5-F7 during any R-phase capture, or the run must log those keypresses as confounds.
2. `dr2hook_core.dll` (17,971,484 B, 08:41) is the build in use. There are older `.bak` copies in the game directory.
3. N6 still applies: NetworkGuard is not an offline proof. Recommendation: before any write phase, relaunch with `%command%` wrapped in a netns / `unshare -n` or firejail `--net=none`. Note that this also cuts the Steam IPC and may break Steam DRM. Alternatively, add an nftables drop rule by uid/cgroup, and re-run G1.1.

---

## Raw output

### Run A (12:04:44-12:04:47 BRT), relevant excerpts (the middle was truncated by the tool)
```
GAME PID 4113076
--- /proc/<pid>/fd socket inode classification
pid 4112898: 3 socket fds, kinds={'tcp': 1, 'unix': 2}, non-local-peer inet sockets=0
    30882095 tcp 127.0.0.1:57343 -> 127.0.0.1:60882 ESTABLISHED
pid 4112901: 0 socket fds  | 4112902: 0 | 4112905: 0 | 4112964: 0 | 4112997: 0
pid 4112999: 4 socket fds, kinds={'unix': 4}, non-local-peer inet sockets=0
pid 4113076: 39 socket fds, kinds={'unix': 11, 'udp': 27, 'tcp': 1}, non-local-peer inet sockets=0
    53341020 tcp 127.0.0.1:57202 -> 127.0.0.1:57343 ESTABLISHED
    (27 x udp 0.0.0.0:<port> -> 0.0.0.0:0 CLOSE/UNCONN; inodes 53346014-53363473)
--- G0: file sha256 c119f509fc315d8168aa7aaa5d0a1ee8e3570c7220bd6cbb2d337d6725d73442 size 24668160
TimeDateStamp disk 0x605cbae3 mem 0x605cbae3 header[0:0x400] equal: True
.text va=0x1000 n=0x109d22e disk_sha256=c770ba29...f5e5 mem_sha256=86a2d496...f442 DIFF
differing runs: 5 bytes: 25   (listed in G0 above)
--- G0.4 windows: all 12 MATCH
--- G2 samples (x6, identical):
{'t': '12:04:46', 'car': '0x4b254b80', 'con': '0x4b28f100', 'rig': '0x4b29bab0', 'rig_12c0_eq_rig': True, 'rig_12d0': 4, 'con0': '0x141400c30', 'con0_ok': True, 'con8_eq_rig': True, 'rig0_eq_con': True, 'rig_2550': '0x0', 'rig_3b8': '0x0', 'rig_2548_byte': 0, 'rig_2560': '0x0', 'con_cc0': '0x0', 'rig_1338': 'f5762f42cd210242', 'speed_2508': 10.2374}
```

### Run B (12:05:00-12:05:04 BRT), complete for the sections it covered
See the tables above. The verbatim `ss`, `stat`, `maps`, `ls`, and log excerpts are reproduced in `/workspace/validation/gates/runB_raw.txt`.

---

## Addendum 12:07-12:08 BRT: Phase 1a session re-check
- **Sim live:** the +0x1338 timer and +0xdc ring index took 16 distinct values in 200 ms at 12:07:28. There were 2700 ring advances (60.00 Hz) in the 45 s capture.
- **G2 at start (12:07:28) and end (12:08:13):** all sanity checks true; `confound_flags = 0x00` (clean).
- **Rig pointer unchanged:** 0x4b29bab0.
- **Phase 1a results:** see `/workspace/validation/INV-01-phase1a.md`.

---

## Nota posterior (2026-10-04)
Este documento registra a execução de G0/G1/G2 de 2026-09-30; a frase "G3 ... NOT run" vale para aquele momento. O G3 foi executado depois, em 2026-10-01, com PASS: ver `G3-result.md`.
