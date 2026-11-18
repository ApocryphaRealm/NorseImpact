# Changelog

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Rules in impacts/*.ini: On (NorseEvents event or Payload:<name>), When (NorseConditions), MinDamage, Chance; Effect (Niagara or Cascade), Sound, HitStop, SlowMo.
- Defaults: blood and a 0.05 s hit-stop on hits in a fight, a blood burst on a kill.
### Fixed (during the first test)
- Defaults only in a fight (the game sets units up at map load through the same health change); a broken condition reported once.

### Fixed (2026-11-18, refinement)
- Conditions are compiled from the first map load on, not at start-up (they no longer depend on loading after NorseConditions).
### Fixed (2026-11-18, retrospective)
- A cached effect or sound is checked alive before each use and loaded again if the garbage collector took it
  during the map; the slow-motion restore no longer calls through the hit unit once it is gone (it uses the game
  state instead). Found by the new cached-pointer source gate (2026-11-18); built, not yet tested in game.
