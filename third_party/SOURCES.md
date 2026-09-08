# Vendored SDK sources

The SDK headers are copied verbatim from these fixed public upstream commits. They are used only for the x86 GoldSrc / Metamod ABI. No game-private memory offsets, injected hooks, or downloaded game executables are included here.

| Directory | Upstream and revision | Downloaded archive SHA-256 |
| --- | --- | --- |
| `halflife` | [ValveSoftware/halflife](https://github.com/ValveSoftware/halflife/tree/b1b5cf5892918535619b2937bb927e46cb097ba1), `b1b5cf5892918535619b2937bb927e46cb097ba1` | `D3DC66F02538B5DA83718DB33D0ADC49B4F6594E3B48CBA27E22B5AB08D58472` |
| `metamod` | [alliedmodders/metamod-hl1](https://github.com/alliedmodders/metamod-hl1/tree/18a10db686702e8ae9e0fcb5b5febf8881dc9c2d), `18a10db686702e8ae9e0fcb5b5febf8881dc9c2d` | `024055B586EB114D7B6BB79633807252367CDD180DF4BC7780D817EEDDBC85AB` |

Downloaded on 2026-09-05 from:

- [Valve fixed commit archive](https://codeload.github.com/ValveSoftware/halflife/zip/b1b5cf5892918535619b2937bb927e46cb097ba1)
- [Metamod fixed commit archive](https://codeload.github.com/alliedmodders/metamod-hl1/zip/18a10db686702e8ae9e0fcb5b5febf8881dc9c2d)

`halflife` retains headers from `common`, `engine`, `dlls`, `public`, and `pm_shared`, preserving relative paths, plus the upstream `LICENSE`. `metamod` retains the upstream `metamod/*.h` headers plus upstream `GPL.txt`. The large downloaded archives and unused extracted implementation files were removed after copying. Each directory contains a `COMMIT` file.

Valve's license and individual header notices restrict use to non-commercial enhancements to Valve products. Metamod is GPL v2 or later with an HL Engine / Valve MOD linking exception in its headers. See the original [Valve license](halflife/LICENSE) and [Metamod GPL](metamod/GPL.txt); the header copyright and exception notices remain intact.

`plugin/sdk_bridge.h` excludes unused legacy Metamod utility implementation headers that assume AlliedModders' const-modified HLSDK and ANSI Win32 APIs. It forward-declares only the unused HUD argument type, and supplies the standard export macro. All engine/entity/Metamod table layouts come from the unchanged upstream headers. The plugin limits engine table reads/writes to the released Metamod ABI ending at `pfnCheckParm`; the newer Valve `pfnPEntityOfEntIndexAllEntities` tail is intentionally untouched.
