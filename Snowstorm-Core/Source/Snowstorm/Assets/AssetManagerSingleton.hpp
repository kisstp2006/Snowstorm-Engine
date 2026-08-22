#pragma once

#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/Animation/SkinnedMeshImporter.hpp"
#include "Snowstorm/Assets/AssetRegistry.hpp"
#include "Snowstorm/Assets/MaterialAsset.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"

#include "Snowstorm/Assets/MeshCache.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <mutex>
#include <vector>

namespace Snowstorm
{
	class World;
	class Entity;

	class AssetManagerSingleton final : public Singleton
	{
	public:
		using WorldRef = World*;

		AssetManagerSingleton(const WorldRef world)
		    : m_World(world)
		{
		}

		// Drains in-flight async loads: worker jobs capture `this` and push into m_Completed* on finish,
		// so the object must outlive every job it submitted (a World torn down mid-load — smoke exit,
		// project switch, cold cache after a hot reload — would otherwise be written to after death).
		~AssetManagerSingleton() override;

		bool LoadRegistry(const std::filesystem::path& filePath);
		bool SaveRegistry(const std::filesystem::path& filePath) const;

		AssetHandle Import(const std::filesystem::path& path, AssetType type);

		// The import step (AssetRegistry::Scan on the active project's asset directory): gives every
		// source a .meta, registers it, refreshes freshness, saves the registry cache when it changed.
		// Returns every file found (scenes included) for the content browser.
		std::vector<AssetRegistry::ScannedFile> ScanAssets();
		[[nodiscard]] const AssetRegistry& Registry() const { return m_Registry; }
		AssetRegistry& Registry() { return m_Registry; }

		// Bumped whenever the registry's row set changed (scan, hot-reload import/removal): UI that lists
		// assets (content browser, pickers) re-reads when it sees a new value.
		[[nodiscard]] uint64_t RegistryGeneration() const { return m_RegistryGeneration; }

		// Hot reload entry (AssetWatchSystem): a project source was written/created/removed. Re-imports,
		// then swaps the live object per type — textures in place (same bindless slot), meshes/materials
		// by invalidating their runtime components so the resolve systems re-pull.
		void OnSourceChanged(const std::filesystem::path& relPath, AssetType type, bool removed);

		// Editor "Reimport": persist new import settings to the .meta and re-cook/swap the live objects of
		// every part of that source (the settings hash is part of the cook key).
		bool ReimportAsset(AssetHandle handle, const ImportSettings& settings);

		// Import a model file (any Assimp format) as a set of renderable entities — one per submesh,
		// each with Transform + Mesh + Material + Visibility. A per-submesh ".ssmat" is generated next
		// to the model (DefaultLit; diffuse color + diffuse texture from the aiMaterial when present).
		// Returns the created entities (empty on failure). Does NOT save the registry — caller decides.
		std::vector<Entity> ImportModel(const std::filesystem::path& path);

		// The skeleton / animation clip behind a sub-asset handle ("model.gltf?skeleton",
		// "model.gltf?animation=Walk"). Both come out of ONE parse of the source file, cached per file:
		// a character with five clips would otherwise re-read the whole model five times.
		//
		// No on-disk cook cache yet (the mesh/texture caches have one) -- the parse happens once per file
		// per session, and AnimationClip/Skeleton are the shape a .ssanim blob would hold anyway.
		Ref<Skeleton> GetSkeleton(AssetHandle handle);
		Ref<AnimationClip> GetAnimation(AssetHandle handle);

		// The bind-pose geometry and per-vertex skin binding behind a "model.gltf?skinnedmesh=N" handle.
		// Both live for as long as the model is loaded and are SHARED by every entity using this mesh --
		// what is per entity is the skinning OUTPUT, not its input. Null vertex count means "not a skinned
		// mesh handle" (or the source stopped being one).
		struct SkinnedMeshGpu
		{
			Ref<Mesh> BindPose; // vertex buffer = the un-posed vertices the skinning pass reads
			Ref<Buffer> Skin;   // SkinnedVertexWeights per vertex, as a storage buffer
			uint32_t VertexCount = 0;
		};
		const SkinnedMeshGpu* GetSkinnedMesh(AssetHandle handle);

		// The CPU-side bind pose + skin bindings behind the same handle. Only the skinning self-test needs
		// them (to predict what the GPU should produce); the render path lives entirely on the GPU copies.
		const SkinnedSubmesh* GetSkinnedSubmeshCpu(AssetHandle handle);

		Ref<Mesh> GetMesh(AssetHandle handle);

		// Non-blocking mesh fetch: returns the GPU mesh if already resident, else null and kicks off an
		// async load on a JobSystem worker (CPU cook/blob-read off the main thread). The finished GPU
		// upload happens on the main thread in ProcessCompletedLoads(); a later GetMeshAsync then hits the
		// cache. In-flight requests are deduped, so calling this every frame for an unresolved handle is
		// cheap. Whole-file (submesh < 0) handles fall back to the synchronous GetMesh (rare, not hot).
		Ref<Mesh> GetMeshAsync(AssetHandle handle);

		// Main-thread pump: drain worker-completed loads, create their GPU resources, populate the caches.
		// Call once per frame (see AssetLoadService). Does GPU work, so MUST run on the main/render thread.
		void ProcessCompletedLoads();

		// Progress for a loading screen: assets whose async load hasn't finished yet (0 = everything
		// resident). PendingLoadTotal is the high-water mark since the queue was last empty, so a bar can
		// show loaded/total.
		[[nodiscard]] uint32_t PendingLoadCount() const;
		[[nodiscard]] uint32_t PendingLoadTotal() const { return m_PendingTotal; }

		// The names of the assets whose cook/read is running on a worker RIGHT NOW, newest last, so a
		// loading screen can say WHAT it is waiting for instead of showing a bare bar. Entries live for the
		// whole job, which is the point: if a load stalls, its name stays on screen and names the culprit.
		// Safe to call from the main thread while workers run.
		[[nodiscard]] std::vector<std::string> GetLoadActivity() const;

		// True when a bindless texture slot holds its REAL image, not the async magenta placeholder. Slot 0 =
		// untextured (no dependency) counts as resident. A one-shot GPU consumer that samples a slot at build
		// time (the OMM bake) MUST gate on this: the material bakes the slot index the instant it resolves, but
		// the real pixels stream in later, so a bake before residency samples the opaque placeholder.
		[[nodiscard]] bool IsTextureSlotResident(uint32_t slot) const;

		Ref<Shader> GetShader(AssetHandle handle);

		// Resolve a texture handle to a sampled view. `srgb` selects the color space the GPU view
		// interprets: albedo/emissive are sRGB (default), while data maps (normal/metallic-roughness/AO)
		// must be linear or lighting is wrong. The same source texture can be requested in both spaces
		// (it is cached per (handle, srgb)).
		Ref<TextureView> GetTextureView(AssetHandle handle, bool srgb = true);

		// Non-blocking texture fetch: returns a resident view if ready, else a shared placeholder view on a
		// STABLE bindless slot and kicks off an async decode on a JobSystem worker. When the decode+upload
		// finishes (ProcessCompletedLoads, main thread) that slot is rewritten to the real texture — the
		// slot index baked into material constants never changes, so nothing needs patching; the pixels just
		// pop in. Deduped per (handle, srgb).
		Ref<TextureView> GetTextureViewAsync(AssetHandle handle, bool srgb = true);

		/// Unique material instance
		Ref<MaterialInstance> CreateMaterialInstanceUnique(AssetHandle handle);

		/// Cached shared instance (per asset)
		Ref<MaterialInstance> GetMaterialInstance(AssetHandle handle);

		// Drop the cached MaterialInstance for a material handle so the next GetMaterialInstance rebuilds it
		// from the (possibly just-edited) .ssmat on disk. The engine's material hot-reload seam: the editor's
		// material inspector calls this after saving, then marks using-entities Changed so MaterialResolveSystem
		// re-pulls the fresh instance. No-op if the handle was never resolved/cached.
		void ReloadMaterial(AssetHandle handle);

		// Live MSAA: rebuild every cached scene-material pipeline in place at the new sample count. Material
		// instances hold a Ref to the same Pipeline object, so the in-place swap reaches them with no cache
		// eviction or re-resolution. Called (with the GPU drained) when render.msaa changes, alongside the scene
		// target reallocation, so pipelines and targets agree on the sample count. No-op if nothing is cached.
		void RebuildPipelinesForSampleCount(uint32_t samples);

		const AssetMetadata* GetMetadata(AssetHandle handle) const { return m_Registry.GetMetadata(handle); }

		// Visit every registered asset (editor UI: asset picker, content browser).
		void IterateAssets(const std::function<void(const AssetMetadata&)>& fn) const { m_Registry.Iterate(fn); }

		// Look up an existing handle by path+type (0 if not yet imported). Editor content browser.
		AssetHandle FindHandle(const std::filesystem::path& path, const AssetType type) const { return m_Registry.FindHandleByPath(path, type); }

	private:
		Ref<Pipeline> GetOrCreatePipeline(const std::string& fragmentShaderPath);

		// Copy a loaded MaterialAsset's colors/factors/maps onto a base Material (shared by the cached
		// and unique material-instance paths). Resolves each texture handle in the correct color space.
		void ApplyMaterialAsset(Material& base, const MaterialAsset& matAsset);

		// Resolve metadata for a non-zero handle, logging a clear error (once per handle) if it is
		// missing or the wrong type. A scene referencing handles absent from the registry is the
		// classic "registry stale/missing" failure and otherwise fails silently (nothing renders).
		const AssetMetadata* ResolveMetaOrWarn(AssetHandle handle, AssetType expected, const char* what);

	private:
		AssetRegistry m_Registry;

		std::unordered_set<uint64_t> m_WarnedHandles;

		WorldRef m_World;

		std::unordered_map<uint64_t, Ref<Mesh>> m_MeshCache;

		// One parsed skinned model per SOURCE FILE (key = resolved path), plus the per-handle views into
		// it. The parse is the expensive part; the maps below just hand out what it produced.
		struct LoadedSkinnedModel
		{
			Ref<Skeleton> Bones;
			std::unordered_map<std::string, Ref<AnimationClip>> ClipsByName;
			std::vector<SkinnedSubmesh> Submeshes; // CPU side; the GPU buffers are built on first use
		};
		std::unordered_map<std::string, LoadedSkinnedModel> m_SkinnedModelCache;
		std::unordered_map<uint64_t, Ref<Skeleton>> m_SkeletonCache;
		std::unordered_map<uint64_t, Ref<AnimationClip>> m_AnimationCache;
		std::unordered_map<uint64_t, SkinnedMeshGpu> m_SkinnedMeshCache;

		// Parses the source behind `handle` once and caches it. Null when it isn't a skinned model.
		const LoadedSkinnedModel* LoadSkinnedModelFor(AssetHandle handle, const AssetMetadata& meta);
		std::unordered_map<uint64_t, Ref<Shader>> m_ShaderCache;
		// Keyed by (handle, srgb): a texture can be sampled both as sRGB (albedo) and linear (data).
		std::unordered_map<uint64_t, Ref<TextureView>> m_TextureViewCache;       // srgb views
		std::unordered_map<uint64_t, Ref<TextureView>> m_TextureViewCacheLinear; // linear views
		std::unordered_map<uint64_t, Ref<MaterialInstance>> m_MaterialInstanceCache;

		std::unordered_map<std::string, Ref<Pipeline>> m_PipelineCache; // key = fragment-shader path

		// --- Async mesh loading (#84) ---
		// A worker-completed CPU load waiting for main-thread GPU finalize.
		struct CompletedMeshLoad
		{
			AssetHandle Handle{};
			std::string FilePath;
			int SubmeshIndex = -1;
			CookedMesh Cooked; // empty on load failure (still drained so the handle stops being in-flight)
			bool Success = false;
		};

		// Handles with an async load submitted but not yet finalized. Main-thread only (GetMeshAsync +
		// ProcessCompletedLoads both run there), so no lock needed for this set.
		std::unordered_set<uint64_t> m_InFlightMeshes;

		// Worker threads push finished CPU loads here; the main thread drains them in ProcessCompletedLoads.
		// Guarded because producers (JobSystem workers) and the consumer (main thread) race on it.
		std::mutex m_CompletedMutex;
		std::vector<CompletedMeshLoad> m_CompletedMeshes;

		uint32_t m_PendingTotal = 0; // high-water mark of in-flight loads since the queue was last empty

		// --- Load activity (published by workers, read by the loading overlay) ----------------------
		// Its own mutex, deliberately not m_CompletedMutex: publishing "I started X" must never contend
		// with the completed-load hand-off that the main thread drains every frame.
		mutable std::mutex m_ActivityMutex;
		std::vector<std::string> m_Activity;

		// Per-burst accounting for the one-line summary logged when the queue drains. A "burst" is one
		// run of loads with no idle gap -- a level load, or the batch a hot-reload kicks off.
		std::chrono::steady_clock::time_point m_BurstStart{};
		std::atomic<uint32_t> m_BurstCooked{0}; // of the burst's loads, how many parsed/encoded from source

		void BeginActivity(const std::string& name);
		void EndActivity(const std::string& name, const char* kind, double ms, bool cooked);
		void NotePendingLoad(); // ++m_PendingTotal, starting a new burst if the queue was idle

		// --- Async texture loading (#84 increment 2) ---
		// A worker-completed CPU decode waiting for main-thread GPU upload + bindless slot rewrite.
		struct CompletedTextureLoad
		{
			uint64_t Key = 0; // (handle, srgb) cache key — matches m_TextureViewCache/-Linear
			AssetHandle Handle{};
			bool Srgb = true;
			uint32_t Slot = 0; // the stable bindless slot the placeholder view occupies
			CookedTexture Cooked;
			bool Success = false;
			std::string DebugName;
		};

		std::unordered_set<uint64_t> m_InFlightTextures;       // (handle,srgb) keys currently decoding
		std::vector<CompletedTextureLoad> m_CompletedTextures; // guarded by m_CompletedMutex (shared w/ meshes)

		// Shared 1x1 placeholder sampled by materials whose real texture hasn't arrived yet. Each async
		// texture gets its OWN view of this (its own bindless slot), so the slot can be rewritten to the
		// real image on completion independently. Created lazily on the main thread.
		Ref<Texture> m_PlaceholderTexture;
		// Kept alive so their images/slots survive after the placeholder slot is repointed to them. Keyed
		// by (handle,srgb). Value holds the real texture + its view (see ProcessCompletedLoads).
		std::unordered_map<uint64_t, Ref<Texture>> m_ResidentTextures;

		Ref<TextureView> EnsurePlaceholderView(const std::string& debugName);
		// Async decode of `meta`'s source into bindless `slot` (placeholder or a live view's). False if a
		// decode for that (handle, srgb) is already in flight.
		bool KickTextureDecode(const AssetMetadata& meta, bool srgb, uint32_t slot);
		// Swap the live GPU object(s) of one handle after its source or settings changed (see OnSourceChanged).
		void ReloadLive(AssetHandle handle, AssetType type);
		void InvalidateMeshUsers(AssetHandle handle);
		void InvalidateMaterialUsers(AssetHandle handle);
		uint64_t m_RegistryGeneration = 1;

		// Bindless slots still showing the magenta placeholder (real pixels not uploaded yet). A slot is added
		// when its placeholder view is created and removed when ProcessCompletedLoads repoints it to the real
		// image. Main-thread-only (like the caches above), so no lock. Backs IsTextureSlotResident.
		std::unordered_set<uint32_t> m_PlaceholderSlots;
	};
}