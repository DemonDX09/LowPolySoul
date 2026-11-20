# Low Poly Soul (PSP prototype)
Tiny 3D low-poly soulslike: one arena, one boss. By Demon_DX_

## Controls
- Left stick: move
- D-pad L/R: rotate camera, D-pad U/D: tilt camera
- R trigger: lock-on toggle
- Cross: roll (i-frames)
- Square: attack
- L trigger (hold): block
- Triangle: use flask (3)
- Cross after dying / killing boss: restart (auto-restarts after a few seconds)

## Boss moves (all telegraphed)
- Overhead swing: raises sword, then chops
- Slam: sword high + orange glow, shockwave around him (roll or back off)
- Lunge: yellow glow, dashes at you
Boss gets faster under 50% HP.

## Build the EBOOT (pick one)
1. GitHub (no installs): make a repo, upload everything here (keep the .github folder),
   open the Actions tab, run "Build EBOOT", download the artifact -> EBOOT.PBP
2. Docker: `docker run --rm -v "$PWD":/src -w /src ghcr.io/pspdev/pspdev make`
3. Local: install PSPDEV (github.com/pspdev/pspdev), then run `make`

## Run
- PPSSPP: open EBOOT.PBP directly
- Real PSP (custom firmware): copy to ms0:/PSP/GAME/LowPolySoul/EBOOT.PBP
