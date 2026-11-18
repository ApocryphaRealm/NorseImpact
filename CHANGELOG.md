# Changelog

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Rules in impacts/*.ini: On (NorseEvents event or Payload:<name>), When (NorseConditions), MinDamage, Chance; Effect (Niagara or Cascade), Sound, HitStop, SlowMo.
- Defaults: blood and a 0.05 s hit-stop on hits in a fight, a blood burst on a kill.
### Fixed (during the first test)
- Defaults only in a fight (the game sets units up at map load through the same health change); a broken condition reported once.
