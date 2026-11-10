# Third-party code

Ammonite's own code is MIT (see [LICENSE](LICENSE)). The code below is by others and
keeps its own license. Except where noted, it is included as a git submodule, so its
license text is in its own folder after a `git clone --recursive`.

| Project | License | Used by | Where |
|---|---|---|---|
| [libDaisy](https://github.com/electro-smith/libDaisy) (Electrosmith) | MIT | Seed firmware | `lib/libDaisy` |
| [DaisySP](https://github.com/electro-smith/DaisySP) (Electrosmith) | MIT | Seed firmware, simulator, Seed plugin | `lib/DaisySP` |
| [DaisySP-LGPL](https://github.com/electro-smith/DaisySP-LGPL) (Electrosmith): `ReverbSc` | **LGPL-2.1** | Seed firmware, simulator, Seed plugin | `lib/DaisySP/DaisySP-LGPL`; the Seed plugin's copy, with a larger buffer, is `plugin/engine/reverbsc.*` with the license in `plugin/engine/LICENSE-reverbsc.txt` |
| [DPF](https://github.com/DISTRHO/DPF) (DISTRHO Plugin Framework, Filipe Coelho) | ISC | Seed plugin, Ammonite Desktop | `plugin/lib/DPF`, `desktop/lib/DPF` |
| [CHOC](https://github.com/Tracktion/choc) (Julian Storer), inside DPF | ISC | Ammonite Desktop (its WebView panel) | `desktop/lib/DPF/distrho/extra/choc` |
| [CLAP](https://github.com/free-audio/clap) headers (Alexandre Bique), inside DPF | MIT | Seed plugin, Ammonite Desktop | `desktop/lib/DPF/distrho/src/clap` |
| Microsoft WebView2 loader, embedded by CHOC | BSD-3-Clause | Ammonite Desktop | inside CHOC's `choc_WebView.h` |

Ammonite Desktop does not use `ReverbSc`; it has a reverb of its own.

## The LGPL-2.1 and the Seed's binaries

`ReverbSc` is linked statically into the Seed firmware (`ammonite.bin`) and the Seed plugin.
The complete source of both, `ReverbSc` included, is in this repository, with the build
scripts to rebuild them (`ammonite.ps1 build`, `plugin\plugin.ps1 build`), so you can change
`ReverbSc` and relink. The full LGPL-2.1 text is in `plugin/engine/LICENSE-reverbsc.txt`
and in `lib/DaisySP/DaisySP-LGPL`.

## In the releases

The release zips carry these notices: `licenses/` in the Seed plugin's zip, `THIRD-PARTY.txt`
in Ammonite Desktop's.
