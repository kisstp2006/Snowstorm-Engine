#pragma once

// Shared test fixture: a hand-authored skinned glTF written to a temp directory. Kept in one place so
// the import test and the playback test exercise the SAME model -- if the two drifted apart, a passing
// import test would say nothing about what playback actually loads.

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace SnowstormTests
{
	// A hand-authored skinned glTF, written out by the test rather than committed: the numbers below ARE
	// the expected result, so the fixture and the assertions can't drift apart, and there is no asset path
	// for the test to depend on.
	//
	// The model is a 2x4 quad standing on the origin, bound to two bones:
	//   Root  at (0,0,0)      <- the two bottom vertices
	//   Child at (0,2,0)      <- the two top vertices
	// and two clips, both driving the same 90-degree rotation about +Z over one second but on different
	// bones -- "Spin" turns Root (so the whole quad swings), "Bend" turns Child (so only the top does).
	// Two clips that differ in WHICH bone they touch is what makes a crossfade between them observable:
	// halfway through the blend both bones are half-rotated, which neither clip produces on its own.
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
  "animations": [
    {
      "name": "Spin",
      "samplers": [ { "input": 5, "output": 6, "interpolation": "LINEAR" } ],
      "channels": [ { "sampler": 0, "target": { "node": 1, "path": "rotation" } } ]
    },
    {
      "name": "Bend",
      "samplers": [ { "input": 5, "output": 6, "interpolation": "LINEAR" } ],
      "channels": [ { "sampler": 0, "target": { "node": 2, "path": "rotation" } } ]
    }
  ],
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
}
