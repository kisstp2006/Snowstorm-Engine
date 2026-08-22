#include "SkinnedModelCache.hpp"

#include "Snowstorm/Core/EnginePaths.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMagic = 0x4E415353; // "SSAN"
		constexpr uint32_t kVersion = 1;

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t SourceKey = 0;
		};

		// --- writing ---------------------------------------------------------------------------------
		template <typename T>
		void Write(std::ostream& out, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			out.write(reinterpret_cast<const char*>(&value), sizeof(T));
		}

		void WriteString(std::ostream& out, const std::string& value)
		{
			Write(out, static_cast<uint32_t>(value.size()));
			out.write(value.data(), static_cast<std::streamsize>(value.size()));
		}

		template <typename T>
		void WriteVector(std::ostream& out, const std::vector<T>& values)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			Write(out, static_cast<uint64_t>(values.size()));
			if (!values.empty())
			{
				out.write(reinterpret_cast<const char*>(values.data()),
				          static_cast<std::streamsize>(values.size() * sizeof(T)));
			}
		}

		// --- reading ---------------------------------------------------------------------------------
		template <typename T>
		bool Read(std::istream& in, T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			in.read(reinterpret_cast<char*>(&value), sizeof(T));
			return static_cast<bool>(in);
		}

		bool ReadString(std::istream& in, std::string& value)
		{
			uint32_t size = 0;
			if (!Read(in, size) || size > (1u << 20)) // a bone name is never a megabyte: a corrupt blob
			{
				return false;
			}
			value.resize(size);
			if (size > 0)
			{
				in.read(value.data(), size);
			}
			return static_cast<bool>(in);
		}

		template <typename T>
		bool ReadVector(std::istream& in, std::vector<T>& values)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			uint64_t count = 0;
			if (!Read(in, count))
			{
				return false;
			}
			// Guard before resizing: a corrupt count would otherwise try to allocate it.
			if (count > (1ull << 32))
			{
				return false;
			}
			values.resize(static_cast<size_t>(count));
			if (count > 0)
			{
				in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
			}
			return static_cast<bool>(in);
		}
	}

	std::filesystem::path SkinnedModelCache::GetCachePath(const AssetHandle sourceHandle)
	{
		std::filesystem::path path = EnginePaths::CacheDirectory() / "animation";
		path /= sourceHandle.ToString();
		path += ".ssanim";
		return path;
	}

	bool SkinnedModelCache::Save(const AssetHandle sourceHandle, const uint64_t sourceKey, const SkinnedModel& model)
	{
		const std::filesystem::path path = GetCachePath(sourceHandle);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			SS_CORE_WARN("SkinnedModelCache: could not create '{}': {}", path.parent_path().string(), ec.message());
			return false;
		}

		// temp + rename: a crash mid-write must not leave a blob whose header already looks valid.
		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
			{
				return false;
			}

			Header header{};
			header.SourceKey = sourceKey;
			Write(out, header);

			// Skeleton. Bones are written in index order, which IS the parent-before-child order the
			// Skeleton invariant guarantees -- so reading them back in order rebuilds the same hierarchy.
			const Skeleton& skeleton = model.Bones;
			Write(out, skeleton.GetBoneCount());
			for (uint32_t bone = 0; bone < skeleton.GetBoneCount(); ++bone)
			{
				WriteString(out, skeleton.GetBoneName(bone));
				Write(out, skeleton.GetParentBoneIndex(bone));
				Write(out, skeleton.GetRestPose(bone));
				Write(out, skeleton.GetInverseBindMatrix(bone));
			}

			Write(out, static_cast<uint32_t>(model.Clips.size()));
			for (const AnimationClip& clip : model.Clips)
			{
				WriteString(out, clip.GetName());
				Write(out, clip.GetDuration());
				Write(out, clip.GetTrackCount());
				for (uint32_t track = 0; track < clip.GetTrackCount(); ++track)
				{
					WriteString(out, clip.GetTrackBoneName(track));
					const BoneTrack& data = clip.GetTrack(track);
					WriteVector(out, data.TranslationTimes);
					WriteVector(out, data.TranslationKeys);
					WriteVector(out, data.RotationTimes);
					WriteVector(out, data.RotationKeys);
					WriteVector(out, data.ScaleTimes);
					WriteVector(out, data.ScaleKeys);
				}
			}

			Write(out, static_cast<uint32_t>(model.Submeshes.size()));
			for (const SkinnedSubmesh& submesh : model.Submeshes)
			{
				WriteString(out, submesh.Name);
				Write(out, submesh.MaterialIndex);
				WriteVector(out, submesh.Mesh.Vertices);
				WriteVector(out, submesh.Mesh.Indices);
				WriteVector(out, submesh.Skin);
			}

			if (!out)
			{
				out.close();
				std::filesystem::remove(temp, ec);
				return false;
			}
		}

		std::filesystem::rename(temp, path, ec);
		if (ec)
		{
			std::filesystem::remove(temp, ec);
			return false;
		}
		return true;
	}

	std::optional<SkinnedModel> SkinnedModelCache::Load(const AssetHandle sourceHandle, const uint64_t sourceKey)
	{
		const std::filesystem::path path = GetCachePath(sourceHandle);
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
		{
			return std::nullopt;
		}

		Header header{};
		if (!Read(in, header) || header.Magic != kMagic || header.Version != kVersion || header.SourceKey != sourceKey)
		{
			return std::nullopt; // stale, foreign or older: the caller re-imports and overwrites
		}

		SkinnedModel model;

		uint32_t boneCount = 0;
		if (!Read(in, boneCount) || boneCount > (1u << 16))
		{
			return std::nullopt;
		}
		std::vector<glm::mat4> inverseBinds(boneCount);
		for (uint32_t bone = 0; bone < boneCount; ++bone)
		{
			std::string name;
			uint32_t parent = Skeleton::NullIndex;
			BoneTransform rest;
			if (!ReadString(in, name) || !Read(in, parent) || !Read(in, rest) || !Read(in, inverseBinds[bone]))
			{
				return std::nullopt;
			}
			model.Bones.AddBone(std::move(name), parent, rest);
		}
		model.Bones.Finalize(); // derives the model-space rest pose; the authored inverse binds go back after
		for (uint32_t bone = 0; bone < boneCount; ++bone)
		{
			model.Bones.SetInverseBindMatrix(bone, inverseBinds[bone]);
		}

		uint32_t clipCount = 0;
		if (!Read(in, clipCount) || clipCount > (1u << 16))
		{
			return std::nullopt;
		}
		for (uint32_t c = 0; c < clipCount; ++c)
		{
			std::string name;
			float duration = 0.0f;
			uint32_t trackCount = 0;
			if (!ReadString(in, name) || !Read(in, duration) || !Read(in, trackCount) || trackCount > (1u << 16))
			{
				return std::nullopt;
			}
			AnimationClip clip(std::move(name));
			clip.SetDuration(duration);
			for (uint32_t t = 0; t < trackCount; ++t)
			{
				std::string boneName;
				if (!ReadString(in, boneName))
				{
					return std::nullopt;
				}
				BoneTrack& track = clip.GetTrack(clip.AddTrack(std::move(boneName)));
				if (!ReadVector(in, track.TranslationTimes) || !ReadVector(in, track.TranslationKeys) ||
				    !ReadVector(in, track.RotationTimes) || !ReadVector(in, track.RotationKeys) ||
				    !ReadVector(in, track.ScaleTimes) || !ReadVector(in, track.ScaleKeys))
				{
					return std::nullopt;
				}
			}
			model.Clips.push_back(std::move(clip));
		}

		uint32_t submeshCount = 0;
		if (!Read(in, submeshCount) || submeshCount > (1u << 16))
		{
			return std::nullopt;
		}
		for (uint32_t sm = 0; sm < submeshCount; ++sm)
		{
			SkinnedSubmesh submesh;
			if (!ReadString(in, submesh.Name) || !Read(in, submesh.MaterialIndex) ||
			    !ReadVector(in, submesh.Mesh.Vertices) || !ReadVector(in, submesh.Mesh.Indices) ||
			    !ReadVector(in, submesh.Skin))
			{
				return std::nullopt;
			}
			model.Submeshes.push_back(std::move(submesh));
		}

		return model;
	}
}
