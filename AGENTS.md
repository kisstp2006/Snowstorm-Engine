# AGENTS.md — Snowstorm Engine

3D game engine with an abstraction over the rendering backend. Currently Vulkan-only (DirectX 12
planned). Windows-only for now. Public domain (`UNLICENSE.txt`). The engine is Hazel-inspired
(`Ref`/`Scope`, `Layer`/`LayerStack`, `SS_*` macros, instrumentation profiler) but has grown its
own EnTT-based ECS, a Systems/Singletons/Services architecture, a Vulkan RHI, an asset system, and
an ImGui editor.

> This repo is consumed as a git **submodule** of `MegatronJeremy/RG2`, but it has its own
> independent history and `master` branch. Develop it as a standalone project.

## Build & run

Toolchain: **CMake + vcpkg**, no Python. `vcpkg` is a git **submodule** and dependencies are
declared in the root `vcpkg.json` manifest; `CMakePresets.json` is the single configure entry point
(generator, pinned MSVC toolset, vcpkg toolchain file, overlay ports). The vcpkg toolchain bootstraps
`vcpkg.exe` and installs the manifest on the first configure.

```
git clone --recursive <repo>          # or: git submodule update --init vcpkg
cmake --preset default                # VS 2026 / v145 -> build/Snowstorm.slnx
cmake --build --preset debug          # or open build/Snowstorm.slnx in Visual Studio
ctest --preset debug                  # Catch2 unit tests
```

`Scripts/Generate-Solution.bat` wraps the first two lines for double-click use. Visual Studio also
understands the presets directly (*File > Open > Folder*, pick a preset). There is a `vs2022`
preset (v143) for machines without VS 2026 — hosted CI uses it. **Snowstorm-Editor** is the
startup project; the debugger working directory is the repo root, so relative `Assets/...` paths
resolve. Vulkan validation layers are wired via `VK_ADD_LAYER_PATH` pointing at the manifest's
installed `bin` dir (`build/vcpkg_installed/x64-windows/bin`), both in the VS debugger environment
and baked into Debug builds (`SS_VULKAN_LAYER_PATH`).

**The MSVC toolset pin must match what vcpkg builds with.** vcpkg compiles dependencies with the
newest installed MSVC; the preset pins the project to the same exact version (`toolset` field).
If they drift (e.g. Catch2 built with 14.51, project on 14.44) the static libs reference vectorized-
STL symbols the older runtime lacks → `LNK2019 __std_find_*_trivial_pos_1`. When VS updates, bump
the version in `CMakePresets.json` (14.4x → `v143`, 14.5x → `v145`; the family must match the
version or CMake rejects it). The first run is slow — vcpkg compiles every dependency from source.

## Smoke test (run after non-trivial changes)

The engine has a built-in headless mode: with `SS_SMOKE_FRAMES=N` (CVar `smoke.frames`) each
executable runs N frames and exits cleanly. Run both apps this way after any change substantial
enough to affect runtime behavior (engine/render/ECS/asset code, the frame loop, anything touching
Vulkan) and check the exit code and the log for `[error]`/`[critical]`, Vulkan validation, or
assertion text:

```
$env:SS_SMOKE_FRAMES=120; $env:SS_VALIDATION_NONFATAL=1
build\Snowstorm-Editor\Debug\Snowstorm-Editor.exe
build\Snowstorm-Runtime\Debug\Snowstorm-Runtime.exe
```

`SS_VALIDATION_NONFATAL=1` makes every validation error log instead of asserting on the first one,
so one run surfaces all of them. `SS_VALIDATION_EXTRA=1` additionally enables synchronization and
best-practices validation (noisy, advisory). It needs a **real GPU/display** (Vulkan), so it is a
**local** gate — hosted CI only compiles. GPU resources are named via `SetVulkanObjectName`
(`VK_EXT_debug_utils`), so validation/RenderDoc report e.g. `Swapchain[0]` instead of a raw handle.

The engine also keeps its headless instrumentation hooks (`perf.bench.frames` writes averaged
per-pass GPU timings to JSON; `quality.capture.frames` dumps the final present) for ad-hoc A/B
measurement and RenderDoc/RGP/RGA sessions; there is no longer a scripted baseline-diff gate around
them — compare runs by hand and re-check with the editor's Performance panel.

## Console variables (CVars)

Engine flags go through a small CVar registry (`Snowstorm/Utility/CVar.hpp`) instead of ad-hoc
`std::getenv`. Declare engine-wide CVars in `Snowstorm/Core/EngineCVars.{hpp,cpp}`; each
self-registers and is resolved once at startup by `CVarRegistry::Initialize(argc, argv)` (called in
`EntryPoint.hpp`) from, in increasing priority: **default → environment → CLI**.

A CVar named `validation.extra` is set by env `SS_VALIDATION_EXTRA` **or** CLI `--validation.extra`
(dots→`_`, uppercased, `SS_` prefix for env). Bools accept presence (`--flag`, or env set to
anything but `0`/`false`/`off`/`no`). Run any executable with `--list-cvars` (or `--help`) to print
every CVar with its value, type, env name, and description. Current CVars include `smoke.frames`,
`validation.nonfatal`, `validation.extra`, `sim.fixed_hz` (run `--list-cvars` for all). Startup resolution is read-once (env → CLI), but CVars can now also
be **edited live at runtime** from the editor's *Debug > Console Variables* panel (`CVarPanelSystem`):
it lists every CVar with a type-appropriate widget (checkbox/int/float) plus a `name value` command
line, via typed accessors on `ICVar` (`GetKind`/`Get*`/`Set*`). Most engine CVars are read per-frame
through `.Get()` (shadows, IBL, exposure, shadow quality), so edits take effect immediately. A
config-file source is still a planned follow-up.

## Layout

```
Snowstorm-Core/      # STATIC library: all engine code (the only place most work happens)
  Source/Snowstorm/  #   platform-independent engine (Core, ECS, Render, Systems, ...)
  Source/Platform/   #   Vulkan/ (RHI implementation, ~28 files) and Windows/
Snowstorm-Physics-Jolt/ # STATIC module lib: Jolt Physics bound to the ECS (IModule "PhysicsJolt")
Snowstorm-Editor/    # Editor EXECUTABLE — links Core; ImGui dockspace, panels, viewport
Snowstorm-Runtime/   # Editor-free runtime EXECUTABLE — links Core; assembled from {Core} modules only
Assets/              # Shaders, Meshes, Materials, Scenes, Textures (loaded at runtime)
Scripts/             # Generate-Solution.bat (convenience), vcpkg-overlays/ (local port overrides)
Tools/dxc/           # DirectX Shader Compiler (HLSL -> SPIR-V)
```

Core builds to a static lib holding code shared by multiple apps; executables (currently the Editor)
link Core and add it to their include path. All targets are **C++20** (the root `CMakeLists.txt`
sets C++17 globally, but every target overrides to 20 — treat the project as C++20).

**Keep the in-editor shortcut reference current.** The editor has a *Help > Keyboard & Mouse
Shortcuts* window (`Snowstorm-Editor/Source/System/EditorMenuSystem.cpp`, `DrawShortcutsWindow`)
that documents every keyboard/mouse binding. Whenever you add, remove, or change a shortcut (camera
controls, gizmo keys, framing, save, selection, hierarchy actions, …), update that window in the
same change so the docs never drift from the real bindings. It is the single source of truth users
see, so treat it as part of the feature, not an afterthought.

## Architecture (Core)

- **Entry point:** clients define `Snowstorm::CreateApplication()`; `Core/EntryPoint.hpp` provides
  `main` (inits logging, wraps `Run()` in profiler sessions). `Application` owns the window, the
  `LayerStack`, the `EventBus`, and the `ServiceManager` (singleton via `Application::Get()`).
- **Modules:** an executable is assembled from `IModule`s (`Core/Module.hpp`; Unreal IModuleInterface /
  ezEngine plugin shape): `Application(name, Modules<CoreModule, EditorModule>())`. `ModuleRegistry`
  orders them by `Dependencies()` and calls `RegisterTypes` → `RegisterServices(ServiceManager&)` once at
  startup, then `RegisterWorld(World&)` for **every** World the app creates (from the `World` ctor). A
  module checks `World::Type()` (`Game`/`Editor`/`Utility`, cf. Unreal EWorldType) to decide what to add —
  `EditorModule` puts its UI systems only on Editor worlds. `CoreModule` is the engine itself (job system,
  GPU services, the built-in systems in phase order). New engine features that bring systems/services
  belong in a module, not in a hand-written registration list. Modules are linked statically today;
  loading one from a DLL changes only how the `IModule` instance is obtained.
- **ECS:** EnTT-backed. `World`/`Entity` (`World/`), components in `Components/`, behavior in
  `Systems/` (managed by `SystemManager`), cross-cutting state in `Singletons/` (`SingletonManager`),
  and `Service/` for longer-lived services. Components self-register for reflection via **RTTR** plus
  the editor/serializer registry from a per-component static initializer (`RTTR_REGISTRATION { ... }` +
  `AUTO_REGISTER_COMPONENT(T)` in each `Components/*.cpp`; see `Components/ComponentRegistry.hpp`).
  Core is a static lib, so the executables link it `WHOLE_ARCHIVE` to keep those initializer TUs.
- **Data-parallelism is a first-class option for systems — always consider it.** When adding a new
  system (or extending/reworking one), explicitly ask whether its per-entity work is *pure and
  independent* (each entity reads/writes only its OWN components, no shared accumulator, no renderer/
  asset-manager/singleton calls, no `TrackedRegistry` mutation APIs in the loop) and would benefit from
  running across `JobSystem` workers. If so, use the existing primitives instead of a hand-rolled serial
  loop: `System::ParallelForEach<Read<T>/Write<T>...>` for in-place per-entity updates (the DOTS
  IJobEntity model; RotatorSystem is the reference), or `JobSystem::ParallelGather<T>(count, body, emit)`
  for parallel filter/collect into a list (VisibilitySystem's frustum cull is the reference). Both take
  a grain size, degrade to an inline serial pass for small N (parallel only when it pays), gate on the
  `ecs.parallel` CVar for a pure serial-vs-parallel A/B, and preserve deterministic (bit-identical)
  output so `ChangedView`/draw order stay stable. Most systems will NOT qualify (they submit to the
  renderer, touch singletons, run scripts, or scatter into shared state) — those stay serial on plain
  `System`, and that's the correct call, not a missed optimization. The point is to make the
  parallel-vs-serial decision *consciously* each time, not default to serial by habit. Note the current
  ceiling: the O(n) post-barrier change-mark (#91) caps end-to-end speedup at scale even when the
  compute parallelizes near-linearly — measure with `--ecs.benchmark` rather than assuming a win.
- **Scripting (native):** `ScriptComponent{ClassName}` names a `ScriptableEntity` subclass registered
  by a module (`SS_REGISTER_SCRIPT(Type)` in `RegisterTypes`; `ScriptRegistry`). `ScriptSystem` owns the
  lifecycle (Unity MonoBehaviour order): on Edit→Play it creates `ScriptRuntimeComponent` instances
  (`OnCreate`, then `OnStart` before the first `OnUpdate`), delivers `ScriptEventQueue` physics events
  (`OnCollision*/OnTrigger*`) before the tick, and on Play→Edit runs `OnDestroy`. `World` guarantees
  `OnDestroy` on entity destroy and scene clear. The **`FixedUpdate` phase** (between Logic and
  AssetSync) is driven by a `SystemManager` accumulator at `sim.fixed_hz` (max 4 steps/frame; the
  phase's systems see the fixed dt; `FixedAlpha()` for interpolation) — `ScriptFixedSystem` calls
  `OnFixedUpdate`, the physics step goes there too. Example: `Snowstorm-Editor/.../Examples/Scripts/OrbitScript`.
- **Physics (Jolt, Hazel-CE shape):** the engine-side half lives in Core — `Physics/` (`PhysicsTypes`
  `EBodyType/EForceMode/EActorAxis/ECollisionDetectionType/ECollisionComplexity/ContactType`,
  `ColliderMaterial`, `PhysicsSettings` + `PhysicsSystem::GetSettings()`, `PhysicsLayerManager`
  (named layers + collision matrix, layer 0 "Default"), `SceneQueries` (`RayCastInfo`/`SceneQueryHit`))
  and `Components/PhysicsComponents.hpp` (`RigidBodyComponent{BodyType, LayerID, Mass, LinearDrag,
  AngularDrag, DisableGravity, IsTrigger, CollisionDetection, Initial*Velocity, Max*Velocity,
  LockedAxes}`, `CompoundColliderComponent`, `Box/Sphere/CapsuleColliderComponent{…, Material}`,
  `MeshColliderComponent{ColliderAsset, SubmeshIndex, Material, CollisionComplexity}`), so scenes
  serialize without the backend. The backend is `Snowstorm-Physics-Jolt/` (IModule "PhysicsJolt"),
  **native Jolt, no abstraction layer**: `JoltPhysics/JoltScene` (the World's physics singleton: the
  `JPH::PhysicsSystem`, bodies by entity UUID, contact events → `ScriptEventQueue`, `CastRay`,
  `Teleport`, `DrawDebug`), `JoltBody` (one body, Hazel-named API: forces, velocities, mass/drag,
  sleep, kinematic move), `JoltShapes` (component colliders — the entity's and its RigidBody-less
  children's — → one `StaticCompoundShape`, world scale baked; mesh colliders cooked from the
  MeshLibrary cook cache), `JoltMaterial` (per-shape friction/restitution + combine functions),
  `JoltLayerInterface` (object layer = LayerID + moving bit), `JoltContactListener`, `JoltUtils`
  (the only glm↔Jolt glue), `JoltJobSystem` (Jolt jobs on the engine's `JobSystem`). ECS glue in
  `PhysicsSystems`: write-back (Resolve −10, interpolated by `FixedAlpha` when
  `PhysicsSettings::InterpolateBodies`), body sync (Resolve +10, Edit mode too), step (FixedUpdate),
  debug draw (PreRender → `DebugDrawSingleton`). `physics.debug_draw` / `physics.log_stats`.
  Deferred: `CharacterController`, constraints, `MeshColliderCache` (cooked shape blobs), shape
  casts/overlaps, layer persistence in the project file.
- **Rendering:** backend-agnostic interfaces in `Render/` (`RendererAPI`, `Renderer`, `Pipeline`,
  `Shader`, `Buffer`, `Texture`, `Material`, `RenderGraph`, ...). The concrete implementation lives
  in `Platform/Vulkan/` (volk + Vulkan Memory Allocator + spirv-reflect; shaders compiled to SPIR-V
  via `Tools/dxc`).
- **Scenes:** serialized to/from JSON (`World/SceneSerializer.hpp`, nlohmann_json).
- **Events:** `Events/` hierarchy dispatched through `EventBus`; input bridged in `Input/`.

### Conventions

- Namespace `Snowstorm`. Smart-pointer aliases `Ref<T>` (shared) / `Scope<T>` (unique) with
  `CreateRef` / `CreateScope` — use these, not raw `std::shared_ptr`/`make_unique`, in engine code.
- Macros from `Core/Base.hpp`: `SS_ASSERT` / `SS_CORE_ASSERT`, `BIT(x)`, `SS_BIND_EVENT_FN(fn)`,
  `SS_DEBUGBREAK()`. Logging is `SS_CORE_*` / `SS_*` (spdlog). Asserts compile out unless `SS_DEBUG`;
  use `SS_VERIFY` / `SS_CORE_VERIFY` for checks that must survive release builds.
- Platform code goes behind `SS_PLATFORM_WINDOWS` (see `Core/PlatformDetection.hpp`); the engine
  currently `#error`s on non-Windows.
- Headers are `.hpp`, translation units `.cpp`. Core globs all sources recursively, so a new file
  under `Snowstorm-Core/Source/` is picked up after re-running CMake (re-generate the solution).
- **Formatting (format-on-touch):** the repo has a `.clang-format`. The `lint` CI checks the C++
  files changed by a push/PR and **fails if any touched file isn't fully clang-format-clean**, so the
  codebase formats gradually as files are edited. CI uses **clang-format 22.x** (match it locally —
  version drift changes output). Run `clang-format -i <files>` (or enable format-on-save against the
  repo config) before committing. A tracked `pre-push` hook (`.githooks/pre-push`, plain bash) checks
  the files changed vs `master` + uncommitted (the same set CI gates on) so a lint-failing push is
  blocked before it leaves the machine — **enable it once per clone** with
  `git config core.hooksPath .githooks` (bypass a single push with `git push --no-verify`).
- **Shared-header shader bindings are global — mind `space1` collisions (learned from #60).** A
  resource declared in `Engine/Shaders/Include/Engine.hlsli` is emitted into *every* shader that
  includes it, and with `-fspv-preserve-bindings` (always on) it survives even when unused — so it
  lands in the reflected layout of every pipeline, including the full-screen post passes
  (Fxaa/Sharpen/TemporalResolve), which pair their frag with `Fullscreen.vert` (also includes the
  header) and **park their own cbuffers/textures high in `space1` — bindings 3/4/5/6 — to dodge the
  material bindings 0/1/2**. Adding a new binding to the shared header at one of those slots silently
  collides with a post pass's resource of a *different* descriptor type in the same pipeline (a
  validation error, not a compile error — it only shows in smoke). If a binding is used by only one
  shader family (e.g. the shadow comparison sampler is DefaultLit-only), declare it in that shader's
  `.frag`, NOT the shared header, and gate any C++ that binds it on the reflected layout actually
  having that binding (custom-shader materials like Mandelbrot won't).

## Dependencies (vcpkg, x64-windows)

assimp, EnTT, fmt, glew, glfw3, glm, imgui (vulkan+glfw bindings, docking), imguizmo, rttr,
spdlog, stb, Vulkan SDK + validation layers, vulkan-memory-allocator, gli, volk, spirv-reflect,
nlohmann-json, catch2, tracy, joltphysics (`debugrenderer` feature). The canonical list is the root `vcpkg.json` manifest; the linkage is
in `Snowstorm-Core/CMakeLists.txt`. Keep those two in sync when adding a dependency.

## Git hygiene

`.gitignore` excludes everything generated: `build/`, the vcpkg submodule's build outputs
(`vcpkg/installed|buildtrees|packages|downloads`), `.vs/`, `Assets/cache`, and all solution/project
files (`*.sln`, `*.slnx`, `*.vcxproj*`, `*.cmake`, `CMakeCache.txt`, `ALL_BUILD.*`, `ZERO_CHECK.*`,
`Makefile`). `vcpkg.json` and `CMakePresets.json` are tracked sources despite the blanket `*.json`
ignore. Never commit generated files or compiled artifacts. Commit messages in English.

## Think like a real engine

**Always** check how a serious production engine (Unreal, Unity, Godot, modern in-house) does it
*before* proposing or implementing any design — this is a required step, not an optional prompt.
Name the reference model concretely (e.g. "Unity Clear Flags", "Unreal SkyAtmosphere actor", "Godot
WorldEnvironment Background Mode"), state how that engine actually structures the feature, and only
then deliberately decide how far to go for *this* project. If you're unsure how the reference engines
do it, research it (web search / docs) rather than guessing — a vague "engines usually…" is not
acceptable. The point is to anchor every design decision in a proven pattern so today's choice is a
known subset of the real thing, not an accidental invention.

**Lead with the more rigid, long-term-correct option.** When choosing between a quick patch and the
structurally sound design, *propose the sound one first* and recommend it by default — even if it is
more work — and only fall back to the shortcut when there is a concrete reason (time-box, throwaway
code, the right design needs infra that doesn't exist yet). Don't offer the lazy option as the
headline and the good one as an afterthought. Vuk's stated preference: this should feel like a
professional engine, so bias toward the design that a production codebase would actually ship. A
worked example: when per-entity material overrides needed an editor, the rigid choice was to replace
the fixed `mask + one-field-per-property` struct with a *sparse list of named, typed overrides*
(Unity `MaterialPropertyBlock` / Unreal MID) rather than just bolting a picker onto the old shape —
the latter would have had to be ripped out the moment a third override type appeared. The point is not to build AAA infrastructure
— it's a thesis platform — but to make the simplification a *conscious* choice with the real shape
in view, so today's shortcut is a known subset of the right design rather than an accidental dead
end. Call out which parts are intentionally deferred and why, and prefer shortcuts that are a
*smaller version of* the real thing (so they extend later) over ones that would have to be ripped
out. When the "real" way is genuinely cheap, just do it the real way.

**Counterweight — actively guard against bloat.** "Long-term-correct" is NOT "more layers." Before
adding any new abstraction, base class, wrapper, config knob, or indirection, ask out loud: *does this
earn its keep, or is it speculative?* An abstraction with one caller/subclass, a wrapper that only
saves a few lines, a second way to do something the codebase already does — these are bloat, not
rigor. Prefer the load-bearing primitive over sugar layered on top of it; prefer one clear way to do a
thing over two. On every new feature/implementation, explicitly weigh whether it *adds* surface area
(a concept a future reader must learn, a decision they must make) against what it removes, and say so.
When a proposed piece optimizes the rare case while taxing the common case, that's backwards — cut it.
The bias toward the production-grade design (above) and the bias against bloat are the same instinct:
build the real shape, but only the parts that are actually load-bearing. When in doubt, leave it out —
re-adding a thin wrapper later is cheap; ripping out an entangled one that grew callers is not. A
worked example: a CRTP `EntitySystem` base was built to wrap the `ParallelForEach` primitive for
single-query systems, then cut — it had one subclass, only fit the one-query case (multi-loop systems
drop back to the primitive anyway), and added a "which base do I derive?" decision to every new
system, all to save ~5 lines. The primitive was the real abstraction; the wrapper was bloat.

Worked example — **asset pipeline** (the engine's current biggest simplification):

- **Real engines separate source assets from cooked runtime assets.** The file you drop in
  (`.obj`/`.png`/`.fbx`) is the *source*; an *importer* cooks it once into a GPU-ready artifact
  (mesh → packed vertex/index buffers; texture → BC7/ASTC + mips; shader → SPIR-V/DXIL) plus a
  sidecar `.meta` holding a stable GUID + import settings (cf. Unity's `foo.fbx.meta`). Scenes
  reference the **GUID**, never the path — so moving/renaming a file never breaks references.
- An **asset database** maps `GUID → (source, cooked, dependencies, content hash)`; a **file
  watcher** re-cooks only what changed (and its dependents) and hot-reloads it; the runtime
  **streams** cooked assets asynchronously under a memory budget; builds cook only the transitive
  closure of what scenes actually reference (no dead content shipped).
- **Where Snowstorm is today:** every importable source has a committed **`<file>.meta` sidecar**
  (`Assets/AssetMeta.hpp`) that owns its GUID + import settings (a model's submesh GUIDs under
  `SubAssets`, cf. Unity fileIDs); `AssetRegistry::Scan` (the import step, run at registry load and by
  the content browser) walks the asset dir, writes missing sidecars, registers parts, and refreshes a
  **content-hash freshness key** (size+mtime fast path, FNV-1a on change). `AssetRegistry.json` is a
  machine-local cache of that (gitignored, rebuilt from the sidecars). Mesh/texture/IBL **cook caches**
  (`Engine/cache/`) key on `SourceKey` = content hash ^ import-settings hash, so editing a source OR
  an import setting re-cooks. Mesh/texture loads are **async** (JobSystem cook, main-thread upload,
  placeholder until resident). A **file watcher** (`FileWatcherService`, ReadDirectoryChangesW on a
  thread) feeds `AssetWatchSystem` (AssetSync phase; 250 ms debounce): a changed project source is
  re-imported and **hot-reloaded** (`AssetManagerSingleton::OnSourceChanged` — textures in place into
  the same bindless slot, meshes/materials by invalidating their runtime components so the resolve
  systems re-pull), a new/removed file re-scans, and a `.hlsl/.hlsli` edit recompiles + rebuilds
  pipelines (the old 1 Hz shader poll is gone). Still deliberately missing: streaming under a memory
  budget, compressed texture formats. Treat `AssetRegistry` / `AssetManagerSingleton`
  as the seam where that grows.

## Verify before claiming

- This is graphics code: "renders/looks correct" can only be confirmed by **building and running**
  on a machine with a GPU/display. Headless verification is not possible — say so when you can't run it.
- After non-trivial runtime changes, **build then run both exes headlessly with `SS_SMOKE_FRAMES`**
  (see Smoke test above) — it catches crashes, hangs, and Vulkan validation/assertion errors that
  compilation can't. A clean smoke run is the minimum bar before claiming a runtime change works.
- Confirm behavior against the actual source/build, not from names. Mark unverified statements as
  assumptions.

### Build verification (learned the hard way)

- **Check the build exit code, not a grepped log.** `cmake --build ... | grep -i error` can miss the
  real failure (MSBuild error formatting varies) and report success on a broken build. Always inspect
  `${PIPESTATUS[0]}` / the actual exit status. A failed compile leaves the **previous** exe in place,
  so the app keeps running stale code and every downstream test is meaningless.
- **Confirm the exe was actually rebuilt** before testing behavior: check the binary's timestamp
  (`ls -l build/Snowstorm-Editor/Debug/Snowstorm-Editor.exe`) is newer than your edit. If a "rebuild"
  didn't update the timestamp, the build failed silently — fix that first. This is the #1 cause of
  "my change isn't taking effect."
- **A running editor locks the exe.** `LNK1168: cannot open ... for writing` means a previous instance
  is still alive; `taskkill //IM Snowstorm-Editor.exe //F` before rebuilding. A leftover process also
  means you may be looking at an old build.
- Strip all temporary debug probes (logs, on-screen text) before committing, and `git diff` each
  touched file to catch leftovers — incremental edits during debugging are easy to forget.

### Don't turn the user into your debugger

- Prefer verification you control: headless runs (`SS_SMOKE_FRAMES=N`), startup-time logging, and
  reading source/state. Reserve "please click X and tell me what you see" for genuine final visual
  confirmation, not for diagnosing logic — a manual launch→click→report loop burns the user's time
  and stalls on build/timing artifacts.
- **Keep effort proportional.** Time-box cosmetic/nice-to-have features; if one can't be made to work
  and verified in a couple of clean attempts, drop it rather than rabbit-holing. Commit the larger
  body of working, verified changes promptly instead of leaving it uncommitted while chasing a detail.

### How to debug effectively (don't guess in a loop)

- **Use the instrumentation that already exists BEFORE writing ad-hoc probes.** This engine already has
  rich, always-on timing/state readouts — check them first instead of scattering `SS_CORE_WARN` probes:
  - The editor's **Performance panel** (`Snowstorm-Editor/System/SceneHierarchySystem.cpp`) shows
    per-phase + per-**system** CPU ms, per-**pass GPU** ms (timestamp scopes), draw/batch/instance/
    triangle counts, and cull stats — smoothed and heat-colored. A "which part of the frame is slow"
    question is usually answered by reading this, not by instrumenting.
  - `SystemManager::GetSystemTimingsMs()` / `GetPhaseTimingsMs()` — per-system/phase CPU time (the same
    data the panel draws), queryable in code.
  - `CommandContext::BeginGpuScope`/`CollectGpuScopes` (`RendererService::GetGpuPassTimes()`) — per-pass
    GPU timestamps.
  - The **frame-time watchdog** (`debug.max_frame_ms` CVar / `--max-frame-ms` smoke flag) turns a
    per-frame stall into a headless `[error]` naming the exact frame + duration.
  - The **profiler** (`SS_PROFILE_SCOPE` / `SS_PROFILE_FUNCTION`) for a full cross-thread timeline. Two
    back-ends behind the same macros: **Tracy** (primary, live — connect the Tracy GUI to a running Debug
    build over the network; `TRACY_ENABLE` is on in Debug) and a **headless JSON fallback**
    (`profile.capture_frames` / `profile.capture_path` CVars dump a chrome://tracing / Perfetto file with
    no GUI, for automated/offline trace analysis). Instrumented spots: frame-loop phases, every ECS system,
    and JobSystem worker tasks. One `SS_PROFILE_SCOPE` per lexical scope (it declares a fixed-name RAII
    object — two in the same block is a redefinition; nest them).
  A whole debugging session was once burned scattering probes to find a load spike that the Performance
  panel would have pinned to `RenderSystem`/shadow-fit in one glance (and the fix — read the panel — was
  already built). If the existing readouts genuinely don't cover the spot, ADD a permanent, toggleable
  diagnostic there (extend the panel / add a scope / a CVar-gated log) rather than a throwaway probe —
  see "Build the engine to be debuggable" below. Reserve ad-hoc probes for gaps the standing tools can't
  reach, and strip them before committing.
- **Bisect, don't guess.** When behavior contradicts the code, the bug is somewhere between "what I
  believe is true" and "what's observed." Add a probe that splits that gap in half and *prove* which
  side is wrong, rather than changing code speculatively and re-running. One well-placed probe beats
  five hopeful edits.
- **One assumption per probe; isolate the variable.** Each test should answer exactly one yes/no
  question. If a result is paradoxical (e.g. "metadata valid at registration but absent at render"),
  do not theorize further — put *both* readings in a *single build/run* and compare. Contradictions
  across separate runs usually mean the runs differed (stale exe, different selection), not that the
  code is haunted.
- **Verify the harness before the hypothesis.** Before concluding "the code is wrong," confirm the
  test itself is valid: right binary (timestamp), build actually succeeded, the probe code path is
  even reached. Most "impossible" bugs are a broken test, not broken code.
- **Make the probe observable without a human.** Favor startup-time logs and `SS_SMOKE_FRAMES` runs
  whose output you can read directly. An on-screen-only probe that requires a click is a last resort
  and is itself a debuggability smell (see below).
- **When stuck after ~2 failed attempts, change altitude.** Stop poking the same spot: re-read the
  full function (not a snippet), question the premise, or check the layer above/below (build system,
  RTTR registration, ImGui ID/widget state). Repeating a variant of a failed approach is the signal
  to step back, not to try harder.

### Debugging rendering bugs specifically (lead with observation, not code)

A plausible cause is not a proven cause. On a flickering-texture bug the obvious-looking culprits
(missing mipmaps, near-plane z-fighting, depth precision) were all *wrong* — each was "fixed" before
being proven, wasting three rounds. What actually found it: the user's observations + visual probes.

- **Ask "when does it NOT happen?" before reading code.** Which scene only? Which material only?
  Static camera or only in motion? Each answer eliminates a whole class of causes in one sentence.
- **Static vs motion is the big discriminator.** Garbage/flicker *while the camera is static* ⇒ data
  changing per-frame: a race, sync gap, or undefined behaviour (e.g. non-uniform descriptor indexing).
  Shimmer *only under motion* ⇒ aliasing / mip-LOD / depth precision. Pin this first; it splits the
  search space in half immediately.
- **Bisect with temporary shader probes, not theory.** Force a flat color (geometry vs sampling),
  force texture index 0 (slot vs index), output a value as RGB (index magnitude, `SV_InstanceID`),
  force a mip level (`SampleLevel`). Each probe is one yes/no that halves the space; ~4–5 pin it. This
  is "bisect, don't guess" applied to the GPU — strip every probe before committing.
- **Don't "fix" before the probe proves the cause.** Prove with one probe, *then* change code.
- **Bindless + instancing red flag:** when one draw renders many objects with *different*
  descriptor-array indices, the index is not dynamically uniform → wrap in `NonUniformResourceIndex()`
  and enable the matching `shader*ArrayNonUniformIndexing` device feature. Silent garbage/flicker
  otherwise; it "works" pre-instancing only because each object was its own draw.
- **A wrong fix that's independently useful can stay.** Misdiagnoses (mipmaps, near plane) were real
  improvements on their own — keep them; don't revert good changes just because they missed this bug.
- **Color/hue/gamma bug → suspect the color space and pipeline STAGE before the formula.** On a color
  bug, if two fixes fail in the *same category*, stop tweaking the math and ask: wrong color space
  (linear vs display/sRGB, HDR vs tonemap-compressed, RGB vs signed-chroma like YCoCg) or wrong pipeline
  stage (before vs after tonemap)? Several correct-looking diagnoses that all trace to one structural
  fact = that fact is the bug. Worked example (#44): an in-resolve TAA sharpen corrupted edge colors
  through five formula rewrites (signed-chroma clamp → dark-biased low-pass → pre-tonemap hue shift) —
  all one root cause: **sharpening in linear HDR before the per-channel ACES tonemap**, which curves
  R/G/B differently and turns any overshoot into a hue shift. This is the "change altitude after ~2
  failed attempts" rule applied to color: interrogate placement, not parameters.

**Pipeline-stage invariant (learned from #44).** The **temporal resolve runs in linear HDR —
accumulation only**. Perceptual / display-space operations — **sharpening, CAS, contrast, any
per-channel curve** — belong **after** tonemap (a post-tonemap pass, like FXAA on the LDR present
target), NEVER inside the resolve or any pre-tonemap linear-HDR stage: an overshoot that's a neutral
brightness change in linear becomes a **hue shift** once ACES curves each channel. One pass = one
responsibility: don't bolt a display-space effect onto a linear-HDR pass.

### Build the engine to be debuggable

The deeper fix for "I couldn't verify without the user" is to make state inspectable in code:

- **Expose state to headless inspection.** If you can only confirm a feature by looking at the screen,
  add a non-visual path to read the same truth: a startup/CVar-gated dump, a query function, or a log.
  The inspector's reflection (RTTR) and the `smoke.frames` hook already make a lot of state reachable
  without a GPU — prefer wiring new state through those.
- **Prefer pure, testable cores.** Logic that maps data→data (name formatting, layout math, value
  conversions, asset-handle resolution) should live in free functions that a Catch2 test or a headless
  run can exercise directly — not be entangled in an ImGui draw call that only runs on a click.
- **Fail loud, not silent.** Silent fallbacks (a missing asset resolving to null, an unread metadata
  key, a default value) hide bugs and force interactive spelunking. Log once at `[error]`/`[warn]`
  when an expectation is violated, the way `ResolveAssetName` / the unresolved-handle path do.
- **Name things for diagnosis.** Vulkan objects via `SetVulkanObjectName`, ImGui widgets with stable
  unique IDs, components with reflected type names — so logs, validation, and RenderDoc say
  `Swapchain[0]` / `DIRECTIONAL LIGHT`, not an opaque handle.
- **Treat "I had to add a temporary on-screen probe" as a missing feature.** It usually means that
  state should be permanently visible (a debug overlay / stats panel / CVar dump). Consider promoting
  the probe into a real, toggleable diagnostic instead of deleting it.
