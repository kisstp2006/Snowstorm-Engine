#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "SkinnedGltfFixture.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/AnimationComponents.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Systems/AnimationSystem.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include <glm/gtc/constants.hpp>

using namespace Snowstorm;

namespace
{
	// A World that plays animations, with the fixture registered as an ABSOLUTE path -- absolute registry
	// entries resolve without an active Project, so the test needs no project scaffolding.
	struct AnimationWorld
	{
		SnowstormTests::SkinnedGltfFixture Fixture;
		World W{WorldType::Game};
		AssetHandle SkeletonHandle{0};
		AssetHandle ClipHandle{0};

		AnimationWorld()
		{
			CVars::EcsParallel.Set(false);
			W.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>();
			W.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Play;
			W.GetSystemManager().RegisterSystem<AnimationSystem>(SystemPhase::Logic);

			auto& assets = W.GetSingleton<AssetManagerSingleton>();
			const std::string source = Fixture.GltfPath().generic_string();
			SkeletonHandle = assets.Registry().Import(source + "?skeleton", AssetType::Skeleton);
			ClipHandle = assets.Registry().Import(source + "?animation=Spin", AssetType::Animation);
		}

		Entity MakeAnimated() const
		{
			Entity e = const_cast<World&>(W).CreateEntity("Animated");
			e.AddComponent<SkeletalMeshComponent>().Skeleton = SkeletonHandle;
			auto& animation = e.AddComponent<AnimationComponent>();
			animation.Clip = ClipHandle;
			return e;
		}

		void Step(const int frames, const float dt = 1.0f / 60.0f)
		{
			for (int i = 0; i < frames; ++i)
			{
				W.OnUpdate(Timestep{dt});
			}
		}
	};

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, const float epsilon = 1e-3f)
	{
		return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(epsilon)));
	}
}

TEST_CASE("Playing an animation resolves its assets and produces skinning matrices", "[animation][playback]")
{
	AnimationWorld world;
	Entity entity = world.MakeAnimated();

	world.Step(1);

	REQUIRE(entity.HasComponent<AnimationRuntimeComponent>());
	const auto& runtime = entity.GetComponent<AnimationRuntimeComponent>();
	REQUIRE(runtime.Skeleton);
	REQUIRE(runtime.Clip);
	REQUIRE(runtime.Clip->GetName() == "Spin");
	REQUIRE(runtime.SkinningMatrices.size() == runtime.Skeleton->GetBoneCount());

	// The mapping is built once at bind time, not rebuilt per frame.
	REQUIRE(runtime.TrackToBone.size() == runtime.Clip->GetTrackCount());
	REQUIRE(runtime.TrackToBone[0] == runtime.Skeleton->FindBoneIndex("Root"));
}

TEST_CASE("Animation time advances, loops, and stops when told to", "[animation][playback]")
{
	AnimationWorld world;
	Entity entity = world.MakeAnimated();

	SECTION("Time advances at the clip's speed")
	{
		world.Step(30); // half a second at 60 Hz
		REQUIRE(entity.GetComponent<AnimationComponent>().Time == Catch::Approx(0.5f).margin(0.02f));

		// Halfway through a 90-degree turn: the quad's top must have swung 45 degrees and kept its length.
		const auto& runtime = entity.GetComponent<AnimationRuntimeComponent>();
		const uint32_t child = runtime.Skeleton->FindBoneIndex("Child");
		const glm::vec3 tip = glm::vec3(runtime.SkinningMatrices[child] * glm::vec4(0.0f, 4.0f, 0.0f, 1.0f));
		REQUIRE(glm::length(tip) == Catch::Approx(4.0f).margin(0.05f));
		REQUIRE(tip.x < -0.5f); // rotating towards -X
	}

	SECTION("Speed scales the advance")
	{
		entity.PatchComponent<AnimationComponent>([](AnimationComponent& a) { a.Speed = 2.0f; });
		world.Step(30);
		REQUIRE(entity.GetComponent<AnimationComponent>().Time == Catch::Approx(1.0f).margin(0.04f));
	}

	SECTION("A looping clip wraps instead of freezing at the end")
	{
		world.Step(90); // 1.5 seconds through a 1 second clip
		const auto& runtime = entity.GetComponent<AnimationRuntimeComponent>();
		REQUIRE(entity.GetComponent<AnimationComponent>().Time > 1.0f); // raw time keeps counting...
		REQUIRE(runtime.SampledPose.TimePos == Catch::Approx(0.5f).margin(0.03f)); // ...the SAMPLE wraps
	}

	SECTION("Playing = false freezes time but still poses the skeleton")
	{
		world.Step(30);
		const float frozen = entity.GetComponent<AnimationComponent>().Time;
		entity.PatchComponent<AnimationComponent>([](AnimationComponent& a) { a.Playing = false; });
		world.Step(60);

		REQUIRE(entity.GetComponent<AnimationComponent>().Time == Catch::Approx(frozen));
		// Still posed: a paused animation is not an unposed one.
		REQUIRE(entity.GetComponent<AnimationRuntimeComponent>().SkinningMatrices.size() == 2);
		REQUIRE(entity.GetComponent<AnimationRuntimeComponent>().SampledPose.TimePos == Catch::Approx(frozen).margin(0.02f));
	}
}

TEST_CASE("A skeleton with no clip poses at the bind pose", "[animation][playback]")
{
	AnimationWorld world;
	Entity entity = world.W.CreateEntity("BindPoseOnly");
	entity.AddComponent<SkeletalMeshComponent>().Skeleton = world.SkeletonHandle;
	entity.AddComponent<AnimationComponent>(); // no clip assigned

	world.Step(10);

	const auto& runtime = entity.GetComponent<AnimationRuntimeComponent>();
	REQUIRE(runtime.Skeleton);
	REQUIRE_FALSE(runtime.Clip);
	REQUIRE(runtime.SkinningMatrices.size() == 2);

	// Bind pose means the skinning matrices move nothing at all.
	for (const glm::mat4& matrix : runtime.SkinningMatrices)
	{
		REQUIRE(NearlyEqual(glm::vec3(matrix * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f)), {1.0f, 2.0f, 3.0f}));
	}
}

TEST_CASE("Clearing the clip handle rebinds instead of replaying the old clip", "[animation][playback]")
{
	AnimationWorld world;
	Entity entity = world.MakeAnimated();
	world.Step(30);
	REQUIRE(entity.GetComponent<AnimationRuntimeComponent>().Clip);

	// Swapping the handle must drop the resolved clip AND its bone mapping -- a mapping belongs to a
	// (clip, skeleton) pair, and reusing a stale one would drive the wrong bones.
	entity.PatchComponent<AnimationComponent>([](AnimationComponent& a) { a.Clip = AssetHandle{0}; });
	world.Step(1);

	const auto& runtime = entity.GetComponent<AnimationRuntimeComponent>();
	REQUIRE_FALSE(runtime.Clip);
	REQUIRE(runtime.TrackToBone.empty());
	REQUIRE(runtime.SkinningMatrices.size() == 2); // still posed, at bind pose
}
