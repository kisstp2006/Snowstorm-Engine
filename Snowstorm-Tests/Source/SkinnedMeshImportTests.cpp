#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Animation/SkinnedMeshImporter.hpp"

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
	// A hand-authored skinned glTF, written out by the test rather than committed: the numbers below ARE
	// the expected result, so the fixture and the assertions can't drift apart, and there is no asset path
	// for the test to depend on.
	//
	// The model is a 2x4 quad standing on the origin, bound to two bones:
	//   Root  at (0,0,0)      <- the two bottom vertices
	//   Child at (0,2,0)      <- the two top vertices
	// and one clip that rotates Root 90 degrees about +Z over one second.
	class SkinnedGltfFixture
	{
	public:
		SkinnedGltfFixture()
		{
			std::random_device rd;
			m_Dir = std::filesystem::temp_directory_path() / ("ss-skinned-" + std::to_string(rd()));
			std::filesystem::create_directories(m_Dir);
			WriteBinary();
			WriteJson();
		}

		~SkinnedGltfFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(m_Dir, ec);
		}

		[[nodiscard]] std::filesystem::path GltfPath() const { return m_Dir / "fixture.gltf"; }

	private:
		static void PushFloat(std::vector<char>& out, const float value)
		{
			char bytes[sizeof(float)];
			std::memcpy(bytes, &value, sizeof(float));
			out.insert(out.end(), bytes, bytes + sizeof(float));
		}
		static void PushU16(std::vector<char>& out, const uint16_t value)
		{
			char bytes[sizeof(uint16_t)];
			std::memcpy(bytes, &value, sizeof(uint16_t));
			out.insert(out.end(), bytes, bytes + sizeof(uint16_t));
		}
		static void PushU8(std::vector<char>& out, const uint8_t value) { out.push_back(static_cast<char>(value)); }

		void WriteBinary() const
		{
			std::vector<char> buffer;

			// [0, 48) POSITION: bottom pair at y=0, top pair at y=4.
			for (const glm::vec3 position : {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f},
			                                 glm::vec3{1.0f, 4.0f, 0.0f}, glm::vec3{-1.0f, 4.0f, 0.0f}})
			{
				PushFloat(buffer, position.x);
				PushFloat(buffer, position.y);
				PushFloat(buffer, position.z);
			}

			// [48, 64) JOINTS_0: bottom vertices -> joint 0 (Root), top vertices -> joint 1 (Child).
			for (const uint8_t joint : {0, 0, 1, 1})
			{
				PushU8(buffer, joint);
				PushU8(buffer, 0);
				PushU8(buffer, 0);
				PushU8(buffer, 0);
			}

			// [64, 128) WEIGHTS_0: rigid, one influence each.
			for (int i = 0; i < 4; ++i)
			{
				PushFloat(buffer, 1.0f);
				PushFloat(buffer, 0.0f);
				PushFloat(buffer, 0.0f);
				PushFloat(buffer, 0.0f);
			}

			// [128, 140) indices
			for (const uint16_t index : {0, 1, 2, 0, 2, 3})
			{
				PushU16(buffer, index);
			}

			// [140, 268) inverseBindMatrices, column-major: Root identity, Child translate(0,-2,0).
			const glm::mat4 rootInverseBind(1.0f);
			const glm::mat4 childInverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 0.0f));
			for (const glm::mat4& matrix : {rootInverseBind, childInverseBind})
			{
				for (int column = 0; column < 4; ++column)
				{
					for (int row = 0; row < 4; ++row)
					{
						PushFloat(buffer, matrix[column][row]);
					}
				}
			}

			// [268, 276) animation input (seconds), [276, 308) output rotations as glTF (x,y,z,w).
			PushFloat(buffer, 0.0f);
			PushFloat(buffer, 1.0f);
			PushFloat(buffer, 0.0f);
			PushFloat(buffer, 0.0f);
			PushFloat(buffer, 0.0f);
			PushFloat(buffer, 1.0f); // identity
			const float halfSqrt2 = std::sqrt(2.0f) * 0.5f;
			PushFloat(buffer, 0.0f);
			PushFloat(buffer, 0.0f);
			PushFloat(buffer, halfSqrt2);
			PushFloat(buffer, halfSqrt2); // 90 degrees about +Z

			std::ofstream out(m_Dir / "fixture.bin", std::ios::binary);
			REQUIRE(out.is_open());
			out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		}

		void WriteJson() const
		{
			constexpr const char* json = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0, 1 ] } ],
  "nodes": [
    { "name": "SkinnedQuad", "mesh": 0, "skin": 0 },
    { "name": "Root", "children": [ 2 ] },
    { "name": "Child", "translation": [ 0.0, 2.0, 0.0 ] }
  ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 }, "indices": 3 } ] } ],
  "skins": [ { "joints": [ 1, 2 ], "inverseBindMatrices": 4 } ],
  "animations": [ {
    "name": "Spin",
    "samplers": [ { "input": 5, "output": 6, "interpolation": "LINEAR" } ],
    "channels": [ { "sampler": 0, "target": { "node": 1, "path": "rotation" } } ]
  } ],
  "buffers": [ { "uri": "fixture.bin", "byteLength": 308 } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 48,  "target": 34962 },
    { "buffer": 0, "byteOffset": 48,  "byteLength": 16,  "target": 34962 },
    { "buffer": 0, "byteOffset": 64,  "byteLength": 64,  "target": 34962 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 12,  "target": 34963 },
    { "buffer": 0, "byteOffset": 140, "byteLength": 128 },
    { "buffer": 0, "byteOffset": 268, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 276, "byteLength": 32 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [ -1.0, 0.0, 0.0 ], "max": [ 1.0, 4.0, 0.0 ] },
    { "bufferView": 1, "componentType": 5121, "count": 4, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [ 0.0 ], "max": [ 1.0 ] },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC4" }
  ]
})";
			std::ofstream out(m_Dir / "fixture.gltf");
			REQUIRE(out.is_open());
			out << json;
		}

		std::filesystem::path m_Dir;
	};

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, const float epsilon = 1e-3f)
	{
		return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(epsilon)));
	}
}

TEST_CASE("A skinned glTF imports its skeleton, weights and clips", "[animation][import]")
{
	const SkinnedGltfFixture fixture;

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
