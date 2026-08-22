#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Animation/SkinnedMeshImporter.hpp"
#include "Snowstorm/Assets/SkinnedModelCache.hpp"
#include "SkinnedGltfFixture.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

using namespace Snowstorm;

namespace
{
	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, const float epsilon = 1e-3f)
	{
		return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(epsilon)));
	}
}

TEST_CASE("A skinned glTF imports its skeleton, weights and clips", "[animation][import]")
{
	const SnowstormTests::SkinnedGltfFixture fixture;

	std::string error;
	const std::optional<SkinnedModel> model = ImportSkinnedModel(fixture.GltfPath(), error);
	REQUIRE(error.empty());
	REQUIRE(model.has_value());

	SECTION("The skeleton keeps the file's hierarchy and bind pose")
	{
		const Skeleton& skeleton = model->Bones;
		REQUIRE(skeleton.GetBoneCount() == 2);

		const uint32_t root = skeleton.FindBoneIndex("Root");
		const uint32_t child = skeleton.FindBoneIndex("Child");
		REQUIRE(root != Skeleton::NullIndex);
		REQUIRE(child != Skeleton::NullIndex);
		REQUIRE(root < child); // parents precede children, or the one-pass composition is invalid
		REQUIRE(skeleton.GetParentBoneIndex(root) == Skeleton::NullIndex);
		REQUIRE(skeleton.GetParentBoneIndex(child) == root);

		REQUIRE(NearlyEqual(skeleton.GetRestPose(child).Translation, {0.0f, 2.0f, 0.0f}));

		// The inverse bind came from the FILE, not from the rest pose -- here they happen to agree, which
		// is what makes it checkable: model-space bind * inverse bind must be the identity.
		const glm::mat4 identity = skeleton.GetModelSpaceRestPose(child) * skeleton.GetInverseBindMatrix(child);
		REQUIRE(NearlyEqual(glm::vec3(identity[3]), glm::vec3(0.0f)));
	}

	SECTION("Every vertex is bound to the bone the file assigned it, with normalized weights")
	{
		REQUIRE(model->Submeshes.size() == 1);
		const SkinnedSubmesh& submesh = model->Submeshes.front();
		REQUIRE(submesh.Mesh.Vertices.size() == 4);
		REQUIRE(submesh.Mesh.Indices.size() == 6);
		REQUIRE(submesh.Skin.size() == submesh.Mesh.Vertices.size());

		const uint32_t root = model->Bones.FindBoneIndex("Root");
		const uint32_t child = model->Bones.FindBoneIndex("Child");
		for (size_t v = 0; v < submesh.Skin.size(); ++v)
		{
			const SkinnedVertexWeights& weights = submesh.Skin[v];
			const float total = weights.BoneWeights.x + weights.BoneWeights.y + weights.BoneWeights.z + weights.BoneWeights.w;
			REQUIRE(total == Catch::Approx(1.0f).margin(1e-4));

			// Bottom vertices (y == 0) ride the root, top vertices (y == 4) the child.
			const uint32_t expected = submesh.Mesh.Vertices[v].Position.y < 1.0f ? root : child;
			REQUIRE(weights.BoneIndices[0] == expected);
			REQUIRE(weights.BoneWeights[0] == Catch::Approx(1.0f).margin(1e-4));
		}
	}

	SECTION("The clip is named, is one second long, and drives the root bone")
	{
		REQUIRE(model->Clips.size() == 1);
		const AnimationClip& clip = model->Clips.front();
		REQUIRE(clip.GetName() == "Spin");
		REQUIRE(clip.GetDuration() == Catch::Approx(1.0f).margin(1e-3));
		REQUIRE(clip.GetTrackCount() == 1);
		REQUIRE(clip.GetTrackBoneName(0) == "Root");
		REQUIRE(clip.GetTrack(0).RotationKeys.size() == 2);
	}

	SECTION("Sampling the clip and skinning moves the vertices where the animation says")
	{
		const Skeleton& skeleton = model->Bones;
		const AnimationClip& clip = model->Clips.front();
		const std::vector<uint32_t> mapping = clip.BuildTrackToBoneMapping(skeleton);

		Pose pose;
		clip.Sample(1.0f, false, skeleton, mapping, pose); // fully rotated: 90 degrees about +Z

		std::vector<glm::mat4> skinning;
		ComputeSkinningMatrices(skeleton, pose, skinning);

		const uint32_t root = skeleton.FindBoneIndex("Root");
		const uint32_t child = skeleton.FindBoneIndex("Child");

		// +Z by 90 degrees maps (x,y) -> (-y,x). A bottom vertex rides the root, a top vertex the child,
		// and the child's own bind offset must cancel exactly -- if the inverse bind were dropped or
		// applied on the wrong side, the top of the quad would fly off by 2 units.
		const auto skin = [&](const uint32_t bone, const glm::vec3& point)
		{ return glm::vec3(skinning[bone] * glm::vec4(point, 1.0f)); };

		REQUIRE(NearlyEqual(skin(root, {-1.0f, 0.0f, 0.0f}), {0.0f, -1.0f, 0.0f}));
		REQUIRE(NearlyEqual(skin(root, {1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.0f}));
		REQUIRE(NearlyEqual(skin(child, {1.0f, 4.0f, 0.0f}), {-4.0f, 1.0f, 0.0f}));
		REQUIRE(NearlyEqual(skin(child, {-1.0f, 4.0f, 0.0f}), {-4.0f, -1.0f, 0.0f}));
	}
}

TEST_CASE("Importing a model with no bones reports why instead of returning an empty model", "[animation][import]")
{
	std::string error;
	const std::optional<SkinnedModel> missing = ImportSkinnedModel("does-not-exist.gltf", error);
	REQUIRE_FALSE(missing.has_value());
	REQUIRE_FALSE(error.empty());
}

TEST_CASE("The cooked skinned-model cache round-trips a whole model", "[animation][import]")
{
	const SnowstormTests::SkinnedGltfFixture fixture;
	std::string error;
	const std::optional<SkinnedModel> imported = ImportSkinnedModel(fixture.GltfPath(), error);
	REQUIRE(imported.has_value());

	const AssetHandle handle{0x5353414E54455354ull};
	constexpr uint64_t sourceKey = 0xFEEDFACECAFEBEEFull;
	const std::filesystem::path path = SkinnedModelCache::GetCachePath(handle);
	std::error_code ec;
	std::filesystem::remove(path, ec);

	REQUIRE(SkinnedModelCache::Save(handle, sourceKey, *imported));
	REQUIRE(std::filesystem::exists(path));

	const std::optional<SkinnedModel> loaded = SkinnedModelCache::Load(handle, sourceKey);
	REQUIRE(loaded.has_value());

	SECTION("The skeleton comes back identical, hierarchy and bind pose included")
	{
		REQUIRE(loaded->Bones.GetBoneCount() == imported->Bones.GetBoneCount());
		for (uint32_t bone = 0; bone < imported->Bones.GetBoneCount(); ++bone)
		{
			REQUIRE(loaded->Bones.GetBoneName(bone) == imported->Bones.GetBoneName(bone));
			REQUIRE(loaded->Bones.GetParentBoneIndex(bone) == imported->Bones.GetParentBoneIndex(bone));
			REQUIRE(NearlyEqual(loaded->Bones.GetRestPose(bone).Translation, imported->Bones.GetRestPose(bone).Translation));
			// The inverse binds are AUTHORED, not derived -- if the blob dropped them, Finalize's derived
			// ones would silently take their place and only a differently-posed model would show it.
			REQUIRE(loaded->Bones.GetInverseBindMatrix(bone) == imported->Bones.GetInverseBindMatrix(bone));
		}
	}

	SECTION("Clips, tracks and keys survive")
	{
		REQUIRE(loaded->Clips.size() == imported->Clips.size());
		const AnimationClip& before = imported->Clips.front();
		const AnimationClip& after = loaded->Clips.front();
		REQUIRE(after.GetName() == before.GetName());
		REQUIRE(after.GetDuration() == Catch::Approx(before.GetDuration()));
		REQUIRE(after.GetTrackCount() == before.GetTrackCount());
		REQUIRE(after.GetTrackBoneName(0) == before.GetTrackBoneName(0));
		REQUIRE(after.GetTrack(0).RotationKeys.size() == before.GetTrack(0).RotationKeys.size());
		REQUIRE(after.GetTrack(0).RotationTimes == before.GetTrack(0).RotationTimes);
	}

	SECTION("Geometry and skin bindings survive")
	{
		REQUIRE(loaded->Submeshes.size() == imported->Submeshes.size());
		const SkinnedSubmesh& before = imported->Submeshes.front();
		const SkinnedSubmesh& after = loaded->Submeshes.front();
		REQUIRE(after.Name == before.Name);
		REQUIRE(after.MaterialIndex == before.MaterialIndex);
		REQUIRE(after.Mesh.Vertices.size() == before.Mesh.Vertices.size());
		REQUIRE(after.Mesh.Indices == before.Mesh.Indices);
		for (size_t v = 0; v < before.Skin.size(); ++v)
		{
			REQUIRE(after.Skin[v].BoneIndices == before.Skin[v].BoneIndices);
			REQUIRE(after.Skin[v].BoneWeights == before.Skin[v].BoneWeights);
			REQUIRE(NearlyEqual(after.Mesh.Vertices[v].Position, before.Mesh.Vertices[v].Position));
		}
	}

	SECTION("A changed source (or import setting) misses instead of returning stale geometry")
	{
		REQUIRE_FALSE(SkinnedModelCache::Load(handle, sourceKey + 1).has_value());
	}

	SECTION("A truncated blob misses")
	{
		const auto size = std::filesystem::file_size(path);
		std::filesystem::resize_file(path, size / 2);
		REQUIRE_FALSE(SkinnedModelCache::Load(handle, sourceKey).has_value());
	}

	std::filesystem::remove(path, ec);
}
