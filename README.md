# NorseImpact

What a hit looks and sounds like in the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0). Rules react to
NorseEvents moments (UnitDamaged, UnitDied, UnitHealed, NorsePayload's Payload:<name>), filtered by a NorseConditions
condition, and play the game's own assets: a visual effect (Niagara NS_ or Cascade P_, sprayed away from the cause), a
sound, a hit-stop or slow motion. Survey: `RESEARCH-more-frameworks.md` (#9).

```ini
[Blood on every hit]
On = UnitDamaged
When = InFight
MinDamage = 1
Effect = /Game/Norse/Effects/Blood/FXVillesBloodVFXPack/Particles/Niagara/Real/NS_Blood_Slice.NS_Blood_Slice
HitStop = 0.05
```

Author guide: `package\impacts\README.txt`; defaults `package\impacts\_default.ini` (blood and a short hit-stop on
hits in a fight, a blood burst on a kill; slow motion on a kill is there but off). Screenshot of a kill:
`docs/shot-2-kill.png`.

Every engine call (GameplayStatics PlaySoundAtLocation / SpawnEmitterAtLocation / SetGlobalTimeDilation,
NiagaraFunctionLibrary SpawnSystemAtLocation) checks its parameter block against the engine's own size first and is
refused, with a log line, if the engine changed. Game speed is restored in real time; a new map starts at normal speed.

## Status

0.1.0 tested in game (TEST-RESULT-0.1.0.txt). Not yet: Cascade effects and Payload triggers in game, camera shake,
damage numbers. Build: `xmake f -p windows -a x64 -m release --toolchain=msvc -y`, then `xmake` (needs
`../NorsePatcher`, `../NorseManifest`, `../NorseEvents`).

## Licence

GPL-3.0-or-later (see LICENSE).
