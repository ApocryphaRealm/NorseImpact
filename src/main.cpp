// NorseImpact - what a hit looks and sounds like (UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood), 2026-11-17.
// Rules react to NorseEvents (UnitDamaged, UnitDied, UnitHealed, Payload:<name>) with a NorseConditions condition, and
// play the game's own assets: a visual effect (Niagara NS_ or Cascade P_), a sound, a hit-stop (a near-freeze of game
// time) or slow motion. Every engine call goes through Unreal's reflected functions, its parameter block checked against
// the engine's own size first. Survey: RESEARCH-more-frameworks.md (#9).
//
// impacts\*.ini (NorseManifest order), one [rule] per section:
//   [Blood on every hit]
//   On = UnitDamaged                 ; UnitDamaged, UnitDied, UnitHealed, Payload:<payload name>
//   When = IsEnemy                   ; NorseConditions, about the event's unit (Target. = the other one: the cause)
//   MinDamage = 1                    ; health lost at least this much (UnitDamaged)
//   Effect = /Game/Norse/Effects/Blood/FXVillesBloodVFXPack/Particles/Niagara/Real/NS_Blood_Slice.NS_Blood_Slice
//   EffectZ = 40                     ; cm above the unit's middle
//   Sound = /Game/Norse/Audio/SoundFX/Combat/Abilities/Punch/Ms_PunchAbility.Ms_PunchAbility
//   Volume = 1
//   HitStop = 0.06                   ; seconds (real time) of game time at 5%
//   SlowMo = 0.3, 0.8                ; game speed, seconds (real time)
//   Chance = 100
// Console: ni.list, ni.reload, ni.test <rule name> (plays a rule's effect and sound on the hero).
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseEvents.h"

#include <cmath>
#include <map>
#include <random>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    // ------------------------------------------------------------------ rules
    struct Rule
    {
        std::string name, on, payload, where;
        std::wstring when, effect, sound;
        int cond = 0; // NorseConditions handle
        bool condTried = false; // compile once: a broken condition is reported once, not at every map load
        int minDamage = 0, chance = 100;
        float effectZ = 40, volume = 1, hitStop = 0, slowSpeed = 1, slowFor = 0;
    };
    std::vector<Rule> g_rules;

    using NcCompileFn = int (*)(const char*, char*, int);
    using NcEvalFn = int (*)(int, void*, void*);
    NcCompileFn g_ncCompile = nullptr;
    NcEvalFn g_ncEval = nullptr;
    bool g_mapUp = false; // set at the first map load; conditions are compiled from then on

    template <typename F>
    F ModExport(const wchar_t* mod, const char* name)
    {
        HMODULE m = GetModuleHandleW((g_mod.parent_path() / mod / L"dlls" / L"main.dll").c_str());
        return m ? reinterpret_cast<F>(GetProcAddress(m, name)) : nullptr;
    }

    void CompileConditions()
    {
        if (!g_ncCompile) g_ncCompile = ModExport<NcCompileFn>(L"NorseConditions", "NC_Compile");
        if (!g_ncEval) g_ncEval = ModExport<NcEvalFn>(L"NorseConditions", "NC_Eval");
        for (Rule& r : g_rules)
        {
            if (r.when.empty() || r.cond || r.condTried) continue;
            if (!g_ncCompile)
            {
                NRN_WARN("[%s] When = %s, but NorseConditions is not loaded - the rule runs without it", r.name.c_str(), Narrow(r.when).c_str());
                continue;
            }
            char err[256]{};
            r.condTried = true;
            r.cond = g_ncCompile(Narrow(r.when).c_str(), err, sizeof(err));
            if (!r.cond) NRN_ERROR("[%s] When does not compile: %s - the rule is off", r.name.c_str(), err);
        }
    }

    void LoadRules()
    {
        g_rules.clear();
        fs::path dir = g_mod / "impacts";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        nm::Manifest man;
        man.Resolve(g_mod.parent_path().parent_path());
        files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        for (const auto& f : files)
        {
            Rule* cur = nullptr;
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty()) continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                if (l.front() == L'[' && l.back() == L']')
                {
                    g_rules.push_back({});
                    cur = &g_rules.back();
                    cur->name = Narrow(Trim(l.substr(1, l.size() - 2)));
                    cur->where = where;
                    continue;
                }
                size_t eq = l.find(L'=');
                if (!cur || eq == std::wstring::npos)
                {
                    NRN_WARN("%s: not a [rule] or key = value line - skipped", where.c_str());
                    continue;
                }
                std::wstring k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
                if (k == L"on")
                {
                    std::string on = Narrow(v);
                    if (on.rfind("Payload:", 0) == 0)
                    {
                        cur->on = "Payload";
                        cur->payload = on.substr(8);
                    }
                    else cur->on = on;
                }
                else if (k == L"when") cur->when = v;
                else if (k == L"mindamage") cur->minDamage = _wtoi(v.c_str());
                else if (k == L"chance") cur->chance = std::clamp(_wtoi(v.c_str()), 0, 100);
                else if (k == L"effect") cur->effect = v;
                else if (k == L"effectz") cur->effectZ = static_cast<float>(_wtof(v.c_str()));
                else if (k == L"sound") cur->sound = v;
                else if (k == L"volume") cur->volume = static_cast<float>(_wtof(v.c_str()));
                else if (k == L"hitstop") cur->hitStop = static_cast<float>(_wtof(v.c_str()));
                else if (k == L"slowmo")
                {
                    size_t c = v.find(L',');
                    cur->slowSpeed = static_cast<float>(_wtof(v.c_str()));
                    cur->slowFor = c == std::wstring::npos ? 0.5f : static_cast<float>(_wtof(v.c_str() + c + 1));
                }
                else NRN_WARN("%s: unknown key '%s' - ignored", where.c_str(), Narrow(k).c_str());
            }
        }
        g_rules.erase(std::remove_if(g_rules.begin(), g_rules.end(), [](const Rule& r) {
                          if (r.on.empty()) NRN_WARN("[%s] (%s) has no On = line - left out", r.name.c_str(), r.where.c_str());
                          return r.on.empty();
                      }),
                      g_rules.end());
        NRN_INFO("impacts: %zu rule(s) from %zu file(s)", g_rules.size(), files.size());
        if (g_mapUp) CompileConditions(); // not at start-up: NorseConditions may not have registered its conditions yet (NorseAnimSwap 0.1.0)
    }

    // ------------------------------------------------------------------ engine calls (checked parameter blocks)
    UObject* Cdo(const wchar_t* path)
    {
        try
        {
            return RC::Unreal::UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, path, false);
        }
        catch (...)
        {
            return nullptr;
        }
    }

    // loaded effects and sounds with their object-array slot: a cached asset the garbage collector took during the map
    // is loaded again (checked alive on each use); cleared at each map load
    std::map<std::wstring, std::pair<UObject*, int32_t>> g_assets; // alive-checked in LoadAsset (slot kept beside each)

    UObject* LoadAsset(const std::wstring& path)
    {
        auto& cache = g_assets;
        if (auto it = cache.find(path); it != cache.end())
        {
            if (!it->second.first) return nullptr; // did not load before - not tried again this map
            if (Alive(it->second.first, it->second.second)) return it->second.first;
            NRN_INFO("asset %s was collected since it was loaded - loading it again", Narrow(path).c_str());
        }
        UObject* o = Cdo(path.c_str());
        if (!o)
        {
            static UObject* ksl = nullptr;
            if (!ksl) ksl = Cdo(L"/Script/Engine.Default__KismetSystemLibrary");
            struct FStr
            {
                const wchar_t* data;
                int32_t num, max;
            } str{path.c_str(), static_cast<int32_t>(path.size() + 1), static_cast<int32_t>(path.size() + 1)};
            uint16_t ps1 = Reflect::Get().ParmsSizeOf(ksl, L"MakeSoftObjectPath"), ps2 = Reflect::Get().ParmsSizeOf(ksl, L"Conv_SoftObjPathToSoftObjRef"),
                     ps3 = Reflect::Get().ParmsSizeOf(ksl, L"LoadAsset_Blocking");
            if (ksl && ps1 && ps2 && ps3 && ps1 <= 200 && ps2 <= 200 && ps3 <= 200)
            {
                Buf a;
                std::memcpy(a.b, &str, sizeof(str));
                if (Reflect::Get().Call(ksl, L"MakeSoftObjectPath", a.b, ps1, sizeof(a.b)))
                {
                    size_t pathSize = ps1 - 0x10;
                    Buf b;
                    std::memcpy(b.b, a.b + 0x10, pathSize);
                    if (Reflect::Get().Call(ksl, L"Conv_SoftObjPathToSoftObjRef", b.b, ps2, sizeof(b.b)))
                    {
                        size_t retOff = (pathSize + 7) & ~size_t(7);
                        Buf c;
                        std::memcpy(c.b, b.b + retOff, ps2 - retOff);
                        if (Reflect::Get().Call(ksl, L"LoadAsset_Blocking", c.b, ps3, sizeof(c.b))) std::memcpy(&o, c.b + (ps3 - 8), 8);
                    }
                }
            }
        }
        if (!o) NRN_WARN("asset %s did not load", Narrow(path).c_str());
        cache[path] = {o, o ? IndexOf(o) : -1};
        return o;
    }

    bool CheckedCall(UObject* lib, const wchar_t* fn, uint8_t* parms, uint16_t expect)
    {
        uint16_t ps = Reflect::Get().ParmsSizeOf(lib, fn);
        if (ps != expect)
        {
            static std::set<std::wstring> warned;
            if (warned.insert(fn).second)
                NRN_ERROR("%s has %u bytes of parameters, %u expected - not called (the engine changed)", Narrow(fn).c_str(), ps, expect);
            return false;
        }
        return Reflect::Get().Call(lib, fn, parms, ps, 256);
    }

    // GameplayStatics::PlaySoundAtLocation(ctx @0, Sound @8, Location @16, Rotation @40, Volume @64, Pitch @68, StartTime @72,
    //   Attenuation @80, Concurrency @88, OwningActor @96, InitialParams @104) - 112 bytes
    bool PlaySound(UObject* ctx, UObject* sound, const FVec& at, float volume)
    {
        static UObject* gs = Cdo(L"/Script/Engine.Default__GameplayStatics");
        Buf p;
        std::memcpy(p.b, &ctx, 8);
        std::memcpy(p.b + 8, &sound, 8);
        std::memcpy(p.b + 16, &at, 24);
        float v[3] = {volume, 1.f, 0.f};
        std::memcpy(p.b + 64, v, 12);
        return gs && CheckedCall(gs, L"PlaySoundAtLocation", p.b, 112);
    }

    // NiagaraFunctionLibrary::SpawnSystemAtLocation(ctx @0, System @8, Location @16, Rotation @40, Scale @64, bAutoDestroy @88,
    //   bAutoActivate @89, PoolingMethod @90, bPreCullCheck @91) -> component @96 - 104 bytes.
    // GameplayStatics::SpawnEmitterAtLocation(ctx @0, Emitter @8, Location @16, Rotation @40, Scale @64, bAutoDestroy @88,
    //   PoolingMethod @89, bAutoActivateSystem @90) -> component @96 - 104 bytes.
    UObject* SpawnEffect(UObject* ctx, UObject* fx, const FVec& at, const FRot& rot)
    {
        std::wstring cls = fx->GetFullName(nullptr);
        cls = cls.substr(0, cls.find(L' '));
        bool niagara = cls == L"NiagaraSystem";
        UObject* lib = niagara ? Cdo(L"/Script/Niagara.Default__NiagaraFunctionLibrary") : Cdo(L"/Script/Engine.Default__GameplayStatics");
        if (!lib) return nullptr;
        Buf p;
        std::memcpy(p.b, &ctx, 8);
        std::memcpy(p.b + 8, &fx, 8);
        std::memcpy(p.b + 16, &at, 24);
        std::memcpy(p.b + 40, &rot, 24);
        double scale[3] = {1, 1, 1};
        std::memcpy(p.b + 64, scale, 24);
        p.b[88] = 1; // auto destroy
        if (niagara)
        {
            p.b[89] = 1; // auto activate
            p.b[90] = 0; // no pooling
            p.b[91] = 1; // pre-cull check
        }
        else
        {
            p.b[89] = 0; // no pooling
            p.b[90] = 1; // auto activate
        }
        if (!CheckedCall(lib, niagara ? L"SpawnSystemAtLocation" : L"SpawnEmitterAtLocation", p.b, 104)) return nullptr;
        UObject* comp = nullptr;
        std::memcpy(&comp, p.b + 96, 8);
        return comp;
    }

    // GameplayStatics::SetGlobalTimeDilation(ctx @0, float @8) - 12 bytes
    bool SetTimeDilation(UObject* ctx, float d)
    {
        static UObject* gs = Cdo(L"/Script/Engine.Default__GameplayStatics");
        Buf p;
        std::memcpy(p.b, &ctx, 8);
        std::memcpy(p.b + 8, &d, 4);
        return gs && CheckedCall(gs, L"SetGlobalTimeDilation", p.b, 12);
    }

    // ------------------------------------------------------------------ time (hit-stop / slow motion, restored in real time)
    UObject* g_timeCtx = nullptr; // the unit hit: alive-checked via g_timeCtxIx (it can be destroyed before speed is restored)
    int32_t g_timeCtxIx = -1;
    double g_restoreAt = 0;

    double Now()
    {
        return GetTickCount64() / 1000.0;
    }

    void Slow(UObject* ctx, float speed, float seconds, const char* why)
    {
        if (seconds <= 0) return;
        if (SetTimeDilation(ctx, speed))
        {
            g_timeCtx = ctx;
            g_timeCtxIx = IndexOf(ctx);
            g_restoreAt = std::max(g_restoreAt, Now() + seconds);
            NRN_INFO("%s: game speed %.2f for %.2f s", why, speed, seconds);
        }
    }

    void TimeTick()
    {
        if (g_restoreAt > 0 && Now() >= g_restoreAt)
        {
            UObject* ctx = Alive(g_timeCtx, g_timeCtxIx) ? g_timeCtx : nullptr;
            if (!ctx)
            {
                // the unit is gone: any live game state of the world is a world context for the same call
                std::vector<UObject*> gs;
                try
                {
                    RC::Unreal::UObjectGlobals::FindAllOf(L"GameStateBase", gs);
                }
                catch (...)
                {
                }
                for (UObject* o : gs)
                    if (Alive(o, IndexOf(o)) && o->GetFullName(nullptr).find(L"Default__") == std::wstring::npos) ctx = o;
                NRN_INFO("game speed: the slowed unit is gone - restoring through %s", ctx ? "the game state" : "nothing (no world context)");
            }
            if (ctx) SetTimeDilation(ctx, 1.f);
            NRN_INFO("game speed back to 1");
            g_restoreAt = 0;
        }
    }

    // ------------------------------------------------------------------ reacting
    void Play(const Rule& r, UObject* unit, UObject* other)
    {
        FVec at{};
        if (!GetVec(unit, L"K2_GetActorLocation", at)) return;
        at.Z += r.effectZ;
        FRot rot{};
        FVec from{};
        if (other && GetVec(other, L"K2_GetActorLocation", from)) rot.Yaw = std::atan2(at.Y - from.Y, at.X - from.X) * 180.0 / 3.14159265358979; // spray away from the cause
        std::string done;
        if (!r.effect.empty())
            if (UObject* fx = LoadAsset(r.effect))
                done += SpawnEffect(unit, fx, at, rot) ? " effect" : " effect FAILED";
        if (!r.sound.empty())
            if (UObject* snd = LoadAsset(r.sound))
                done += PlaySound(unit, snd, at, r.volume) ? " sound" : " sound FAILED";
        if (r.hitStop > 0) Slow(unit, 0.05f, r.hitStop, r.name.c_str());
        if (r.slowFor > 0) Slow(unit, r.slowSpeed, r.slowFor, r.name.c_str());
        NRN_INFO("[%s] on %s:%s", r.name.c_str(), Name(unit).c_str(), done.empty() ? " (time only)" : done.c_str());
    }

    void OnEvent(const NE_Event* e, void*)
    {
        if (!e || !e->name || !e->subject) return;
        static std::mt19937 rng{std::random_device{}()};
        const char* text = NE_Text(e);
        for (const Rule& r : g_rules)
        {
            if (r.on != e->name) continue;
            if (r.on == "Payload" && (!text || r.payload != text)) continue;
            if (r.minDamage > 0 && (std::strcmp(e->name, "UnitDamaged") != 0 || e->oldValue - e->newValue < r.minDamage)) continue;
            if (!r.when.empty())
            {
                if (!r.cond) continue; // did not compile, or NorseConditions missing - logged at load
                if (!g_ncEval || !g_ncEval(r.cond, e->subject, e->other)) continue;
            }
            if (r.chance < 100 && static_cast<int>(rng() % 100) >= r.chance) continue;
            try
            {
                Play(r, static_cast<UObject*>(e->subject), static_cast<UObject*>(e->other));
            }
            catch (...)
            {
                NRN_ERROR("[%s] threw", r.name.c_str());
            }
        }
    }

    bool g_subscribed = false;
    void Subscribe()
    {
        if (g_subscribed) return;
        if (const ne::ApiTable* api = ne::Api())
        {
            api->Subscribe("*", &OnEvent, nullptr);
            g_subscribed = true;
            NRN_INFO("listening to NorseEvents");
        }
    }

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Trim(c), arg;
        if (size_t sp = cmd.find(L' '); sp != std::wstring::npos)
        {
            arg = Trim(cmd.substr(sp + 1));
            cmd = cmd.substr(0, sp);
        }
        cmd = Lower(cmd);
        NRN_INFO("command: %s %s", Narrow(cmd).c_str(), Narrow(arg).c_str());
        if (cmd == L"ni.reload") LoadRules();
        else if (cmd == L"ni.list")
            for (const Rule& r : g_rules)
                NRN_INFO("  [%s] on %s%s%s when '%s' effect %s sound %s hitstop %.2f slowmo %.2f/%.2f", r.name.c_str(), r.on.c_str(),
                         r.payload.empty() ? "" : ":", r.payload.c_str(), Narrow(r.when).c_str(), r.effect.empty() ? "-" : "yes",
                         r.sound.empty() ? "-" : "yes", r.hitStop, r.slowSpeed, r.slowFor);
        else if (cmd == L"ni.test" && !arg.empty())
        {
            using HeroFn = void* (*)();
            auto hero = ModExport<HeroFn>(L"NorseRealtimeNative", "NRN_Hero");
            UObject* h = hero ? static_cast<UObject*>(hero()) : nullptr;
            for (const Rule& r : g_rules)
                if (_stricmp(r.name.c_str(), Narrow(arg).c_str()) == 0 && h) Play(r, h, nullptr);
            if (!h) NRN_WARN("ni.test: no hero (NorseRealtimeNative not possessing)");
        }
        else NRN_WARN("unknown command - ni.list | ni.reload | ni.test <rule name>");
        return true;
    }
} // namespace

class NorseImpact : public RC::CppUserModBase
{
  public:
    NorseImpact()
    {
        ModName = L"NorseImpact";
        ModVersion = L"" NIM_VERSION;
        ModDescription = L"What a hit looks and sounds like: effects, sounds, hit-stop and slow motion by rule.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        std::wstring dir;
        int logLevel = 2; // uLogLevel: 0 trace, 1 debug, 2 info, 3 warn, 4 error, 6 off
        for (const auto& raw : ReadLines(g_mod / "NorseImpact.ini"))
        {
            std::wstring l = Trim(raw);
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
            if (l.rfind(L"uLogLevel=", 0) == 0) logLevel = _wtoi(l.c_str() + 10);
        }
        Log::Get().Open((dir.empty() ? g_mod : fs::path(dir)) / "NorseImpact.log", logLevel);
        Log::Get().Write(Level::Error, "NorseImpact v%s - log level %d", NIM_VERSION, logLevel);
        LoadRules();
    }

    auto on_unreal_init() -> void override
    {
        Subscribe();
        try
        {
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                g_mapUp = true;
                Subscribe();
                CompileConditions(); // NorseConditions may have loaded after this mod
                if (g_restoreAt > 0)
                {
                    g_restoreAt = 0; // a new map starts at normal speed
                }
                g_timeCtx = nullptr;
                g_assets.clear();
            });
            RC::Unreal::Hook::RegisterEngineTickPreCallback([](RC::Unreal::UEngine*, float) { TimeTick(); });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"ni.", 3) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseImpact();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
