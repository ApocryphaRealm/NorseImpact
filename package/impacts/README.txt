NorseImpact rules
=================

Every *.ini here is a list of [rules]: when a moment happens (NorseEvents), and a NorseConditions condition holds, play
the game's own effect and sound, and slow time for a moment.

    [Blood on every hit]
    On = UnitDamaged              ; UnitDamaged, UnitDied, UnitHealed, or Payload:<name> (a NorsePayload moment)
    When = IsEnemy                ; optional, NorseConditions - about the unit hit (Target. = who hit it)
    MinDamage = 1                 ; optional: at least this much health lost
    Effect = /Game/Norse/Effects/Blood/FXVillesBloodVFXPack/Particles/Niagara/Real/NS_Blood_Slice.NS_Blood_Slice
    EffectZ = 40                  ; cm above the unit's middle
    Sound = /Game/Norse/Audio/SoundFX/Combat/Abilities/Punch/Ms_PunchAbility.Ms_PunchAbility
    Volume = 1
    HitStop = 0.05                ; seconds of near-frozen game time
    SlowMo = 0.35, 0.6            ; game speed, seconds
    Chance = 100

Effects: a Niagara system (NS_) or a Cascade particle system (P_), sprayed away from whoever caused the hit. Norse's
own blood pack is under /Game/Norse/Effects/Blood/FXVillesBloodVFXPack/. Console: ni.list, ni.reload, ni.test <rule>.
