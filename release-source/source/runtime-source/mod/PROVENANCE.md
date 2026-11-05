# Bootstrap provenance and source boundaries

New `src/loader`, `src/t5/identity.*` and focused tests were written for this Black Ops project. The narrow design follows the user's WaW loader/export and D3D9Ex experience, with documented COM methods replacing engine offsets. `src/t5/scene_probe.*` is the coordinator's separate BO-specific diagnostic implementation. New source is GPL-3.0-only; the GPLv3 license text was copied from the user's WaW project.

| Reference | Exact identity | Use and limits |
|---|---|---|
| `../../CallOfDutyWorldAtWar_VR-next` | Git HEAD `5c06b9dbf20aec8cbc9c5ca4c3fbe61b4e7b8939`; live worktree may contain intentional edits | Read-only loader/resource policy reference. `src/mod/d3d9ex_bootstrap.cpp` SHA-256 `4a0adf33784db7870828a23b658aba234c5f0d4296d0f3a96e1d6d368e77c8b5`. Texture/volume/cube migration lines 264–338 and ResetEx routing 190–223 informed the narrow policy; no T4 addresses copied. |
| `../references/LinkerMod` | `72ee05bbf42dfb2a1893e655788b631be63ea317` | `components/game_mod/d3d9ex.cpp:8–45` Create9Ex opt-in, 48–73 CreateDeviceEx routing, 105–116 managed VB lead; `components/game_mod/r_image.cpp:42–57,77–81,111–115` confirms static texture/volume/cube DEFAULT+DYNAMIC policy. Implementation is newly written and preserves existing buffer usage flags. LinkerMod SP struct offsets are not assumed correct for the installed executable. |
| `references/t5-rtx` | `1c7a49eb78847275f23d9e60052d9d8e9b92b25e`; https://github.com/xoxor4d/t5-rtx | Read-only render-symbol lead for the coordinator, not compiled or copied. A root license file was not found during audit; no t5-rtx implementation was incorporated. `src/main.cpp` explicitly excludes its German version and MP and uses direct absolute memory checks, so its address table is not a full executable identity proof. |
| Installed x86 `C:/Windows/SysWOW64/d3d9.dll` | Export list read with VS dumpbin | Host export ordinal/name layout only. Named APIs resolve by name; unnamed ordinals remain specific to this Windows version. |

Microsoft primary references, checked 2026-09-06:

- [Direct3DCreate9Ex](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-direct3dcreate9ex): documented Ex factory and failure result.
- [CreateDeviceEx](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9ex-createdeviceex): method signature, in/out presentation parameters, null fullscreen mode in windowed mode, adapter-group arrays.
- [D3DPOOL](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpool): MANAGED invalid for Ex; DEFAULT textures need dynamic usage for ordinary CPU locking.
- [D3DUSAGE](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dusage): valid dynamic/resource usage combinations.
- [ResetEx](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9ex-resetex): resource persistence and restrictions on API calls following failed ResetEx.

The optional VR coordinator links the separately pinned, copied WaW beta.1 core under `../prototypes/vr-core`. Its provenance and original licenses live there. Neither original WaW/COD4 worktree is a build dependency.

Frozen KisakBlack MP source (`../phase2/source`, `2a7785269d2f969ecc1ce2931679a8a39536f5dd`) independently corroborates usage-zero managed 2D textures at `src/gfx_d3d/r_image.cpp:393–421`, volume at 497–506, cube at 560–567. Its buffers at `r_buffers.cpp:90,123,191,219` already use DEFAULT. These are semantic reference leads, not proof that every retail SP resource takes those paths.
