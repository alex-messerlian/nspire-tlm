# ChatTLM Setup

`ChatTLM_Setup.tns` is the one document a student opens on a calculator that has never run ChatTLM.
It installs Ndless, which is the loader ChatTLM needs, and it says on its own first screen which
OS versions this build supports.

## Why the app cannot install itself

`chattlm.tns` is native ARM code. The stock TI-Nspire OS refuses to execute native code (that
refusal is exactly what Ndless defeats), so ChatTLM cannot run first and install anything. It
cannot start at all on a calculator without Ndless.

The installer gets in through a different door: it is a **Lua document**, and the stock OS runs Lua.
`stage0.S` is assembled, embedded as a Lua string by `luabin`, and wrapped in a TI document. That
Lua entry point is why a branded setup screen is possible at all.

## What is ours and what is theirs

This is a fork of `vendor/Ndless/ndless/src/installer-6.2`. Three files are unchanged, one differs in
a single line, and four are ours; `tools/eval/gate_installer_exploit.py` fails if that ever changes.

| file | state | why |
|---|---|---|
| `installer.lua` | **byte-identical** | drives the exploit, reports the result over IPC |
| `ipc.lua` | **byte-identical** | the clipboard message channel between the two Lua widgets |
| `template.sed` | **byte-identical** | decides which Lua goes into which widget |
| `stage0.S` | **one line** | the ARM exploit, unchanged except the path of the loader it reads (`ndless/` becomes `chattlm/`). We are not qualified to improve it, and a wrong edit bricks a calculator. |
| `gui.lua` | modified | our wording and the supported-OS notice |
| `Problem1_template.xml` | modified | document name: "ChatTLM Setup" |
| `Makefile` | modified | output and tool paths for this tree |
| `.gitignore` | modified | the build leaves generated Lua and objects in this directory |

## Licence

Ndless is covered by the **Mozilla Public License 1.1** ([`LICENSES/MPL-1.1.html`](../LICENSES/MPL-1.1.html)).
MPL 1.1 permits redistribution. The obligation that binds us is that a file we MODIFY stays MPL and
its source stays available, which is why the modified set is kept to the UI, the loader's path and
the build files, and why this directory ships its source rather than only the built `.tns`.

`gui.lua` is a modified MPL file and remains under MPL 1.1. The Ndless credits string in it is left
intact: it is their work and their names.

Some Ndless subdirectories carry their own `LICENSE.txt`; none of those files are forked here.

## Building

    cd installer && make          # -> build/ChatTLM_Setup.tns

Needs the Ndless SDK on PATH (`vendor/Ndless/ndless-sdk/bin`, `.../toolchain/install/bin`).

## The OS version, and what happens when TI ships a new one

This build targets **OS 6.2.0 – 6.4.0**, because that is what Ndless's 6.2 installer supports. The
exploit is version-specific: a newer OS is not a degraded experience, it is a failure. So the first
screen states the supported range before anything runs, and the `UNSUPPORTED OS` screen says it
again in plain words rather than failing obscurely.

When Ndless supports a newer OS, the sequence is: update `vendor/Ndless`, re-fork the matching
`installer-<ver>` directory, re-apply the UI, rebuild. `gate_installer_exploit.py` is what tells you
whether the re-fork drifted.
