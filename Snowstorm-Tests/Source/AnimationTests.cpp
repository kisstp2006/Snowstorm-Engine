#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Animation/AnimationClip.hpp"
#include "Snowstorm/Animation/Skeleton.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Snowstorm;

namespace
{
	// Root at the origin, one child bone 2 units up its parent's +Y. The simplest hierarchy where a
	// parent's rotation has to reach the child -- which is the whole point of a skeleton.
	Skeleton MakeTwoBoneSkeleton()
	{
		Skeleton skeleton;
		BoneTransform root;
		const uint32_t rootIndex = skeleton.AddBone("Root", Skeleton::NullIndex, root);

		BoneTransform child;
		child.Translation = {0.0f, 2.0f, 0.0f};
		skeleton.AddBone("Child", rootIndex, child);

		skeleton.Finalize();
		return skeleton;
	}

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, const float epsilon = 1e-4f)
	{
		return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(epsilon)));
	}

	// Where a point rigidly attached to a bone ends up once the pose is applied.
	glm::vec3 SkinPoint(const std::vector<glm::mat4>& skinningMatrices, const uint32_t bone, const glm::vec3& point)
	{
		return glm::vec3(skinningMatrices[bone] * glm::vec4(point, 1.0f));
	}
}

TEST_CASE("A skeleton composes its rest pose into model space and inverts it", "[animation]")
{
	const Skeleton skeleton = MakeTwoBoneSkeleton();
	REQUIRE(skeleton.GetBoneCount() == 2);
	REQUIRE(skeleton.FindBoneIndex("Child") == 1);
	REQUIRE(skeleton.FindBoneIndex("Missing") == Skeleton::NullIndex);
	REQUIRE(skeleton.GetParentBoneIndex(0) == Skeleton::NullIndex);
	REQUIRE(skeleton.GetParentBoneIndex(1) == 0);

	// The child sits 2 up in MODEL space, because its parent is at the origin.
	REQUIRE(NearlyEqual(glm::vec3(skeleton.GetModelSpaceRestPose(1)[3]), {0.0f, 2.0f, 0.0f}));

	// Inverse bind undoes exactly that.
	const glm::mat4 identity = skeleton.GetModelSpaceRestPose(1) * skeleton.GetInverseBindMatrix(1);
	REQUIRE(NearlyEqual(glm::vec3(identity[3]), {0.0f, 0.0f, 0.0f}));
}

TEST_CASE("Skinning with the rest pose leaves the mesh in its bind shape", "[animation]")
{
	const Skeleton skeleton = MakeTwoBoneSkeleton();

	Pose pose;
	pose.BoneTransforms = skeleton.GetRestPose();

	std::vector<glm::mat4> skinning;
	ComputeSkinningMatrices(skeleton, pose, skinning);
	REQUIRE(skinning.size() == 2);

	// The identity check that catches almost every skinning sign/order mistake: posing a skeleton at rest
	// must move nothing, whatever the bind pose was.
	for (const glm::mat4& matrix : skinning)
	{
		REQUIRE(NearlyEqual(glm::vec3(matrix[3]), glm::vec3(0.0f)));
		REQUIRE(NearlyEqual(glm::vec3(matrix * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f)), {1.0f, 2.0f, 3.0f}));
	}
}

TEST_CASE("A parent's rotation carries its child, and skinning moves an attached point", "[animation]")
{
	const Skeleton skeleton = MakeTwoBoneSkeleton();

	// Rotate the root 90 degrees about +Z: model-space +Y becomes -X.
	Pose pose;
	pose.BoneTransforms = skeleton.GetRestPose();
	pose.BoneTransforms[0].Rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));

	std::vector<glm::mat4> modelSpace;
	ComputeModelSpaceTransforms(skeleton, pose, modelSpace);
	REQUIRE(NearlyEqual(glm::vec3(modelSpace[1][3]), {-2.0f, 0.0f, 0.0f}));

	// A vertex bound to the child at its bind position (0,2,0) must follow it to (-2,0,0).
	std::vector<glm::mat4> skinning;
	ComputeSkinningMatrices(skeleton, pose, skinning);
	REQUIRE(NearlyEqual(SkinPoint(skinning, 1, {0.0f, 2.0f, 0.0f}), {-2.0f, 0.0f, 0.0f}));
}

TEST_CASE("An animation clip interpolates between its keyframes", "[animation]")
{
	const Skeleton skeleton = MakeTwoBoneSkeleton();

	AnimationClip clip("Wave");
	clip.SetDuration(2.0f);
	BoneTrack& rootTrack = clip.GetTrack(clip.AddTrack("Root"));
	rootTrack.TranslationTimes = {0.0f, 2.0f};
	rootTrack.TranslationKeys = {glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f)};

	Pose pose;
	const std::vector<uint32_t> mapping = clip.BuildTrackToBoneMapping(skeleton);

	SECTION("Halfway between two keys is halfway between their values")
	{
		clip.Sample(1.0f, false, skeleton, mapping, pose);
		REQUIRE(NearlyEqual(pose.BoneTransforms[0].Translation, {5.0f, 0.0f, 0.0f}));
		REQUIRE(pose.TimePos == Catch::Approx(1.0f));
	}

	SECTION("A bone with no track stays at its rest pose")
	{
		clip.Sample(1.0f, false, skeleton, mapping, pose);
		// The child has no keys: it must keep its 2-up offset, not collapse to the origin.
		REQUIRE(NearlyEqual(pose.BoneTransforms[1].Translation, {0.0f, 2.0f, 0.0f}));
		REQUIRE(NearlyEqual(pose.BoneTransforms[1].Scale, glm::vec3(1.0f)));
	}

	SECTION("A non-looping clip holds its last frame")
	{
		clip.Sample(5.0f, false, skeleton, mapping, pose);
		REQUIRE(NearlyEqual(pose.BoneTransforms[0].Translation, {10.0f, 0.0f, 0.0f}));
		REQUIRE(pose.TimePos == Catch::Approx(2.0f));
	}

	SECTION("A looping clip wraps, including from a negative time")
	{
		clip.Sample(2.5f, true, skeleton, mapping, pose);
		REQUIRE(NearlyEqual(pose.BoneTransforms[0].Translation, {2.5f, 0.0f, 0.0f}));

		clip.Sample(-0.5f, true, skeleton, mapping, pose);
		REQUIRE(pose.TimePos == Catch::Approx(1.5f)); // wraps forward, does not run backwards
		REQUIRE(NearlyEqual(pose.BoneTransforms[0].Translation, {7.5f, 0.0f, 0.0f}));
	}
}

TEST_CASE("Rotation keys are slerped, so a rotating bone keeps a constant radius", "[animation]")
{
	const Skeleton skeleton = MakeTwoBoneSkeleton();

	AnimationClip clip("Spin");
	clip.SetDuration(1.0f);
	BoneTrack& rootTrack = clip.GetTrack(clip.AddTrack("Root"));
	rootTrack.RotationTimes = {0.0f, 1.0f};
	rootTrack.RotationKeys = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
	                          glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f))};

	Pose pose;
	clip.Sample(0.5f, false, skeleton, clip.BuildTrackToBoneMapping(skeleton), pose);

	// Halfway through a 90-degree turn is 45 degrees -- and the child must still be 2 units from the root.
	// A lerp of the two quaternions would pass closer than 2, which is the classic "limbs shrink" bug.
	std::vector<glm::mat4> modelSpace;
	ComputeModelSpaceTransforms(skeleton, pose, modelSpace);
	const glm::vec3 childPosition = glm::vec3(modelSpace[1][3]);
	REQUIRE(glm::length(childPosition) == Catch::Approx(2.0f).margin(1e-4));

	const float quarter = std::sqrt(2.0f); // 2 * cos(45deg) = 2 * sin(45deg)
	REQUIRE(NearlyEqual(childPosition, {-quarter, quarter, 0.0f}));
}

TEST_CASE("An authored inverse bind matrix overrides the one derived from the rest pose", "[animation]")
{
	Skeleton skeleton = MakeTwoBoneSkeleton();

	// A file whose skin was authored in a DIFFERENT pose than the rest pose supplies its own inverse bind
	// matrices; the derived ones would skin the mesh into a broken shape.
	const glm::mat4 authored = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -5.0f, 0.0f));
	skeleton.SetInverseBindMatrix(1, authored);
	REQUIRE(skeleton.GetInverseBindMatrix(1) == authored);

	Pose pose;
	pose.BoneTransforms = skeleton.GetRestPose();
	std::vector<glm::mat4> skinning;
	ComputeSkinningMatrices(skeleton, pose, skinning);

	// Bone 1's model-space rest is +2 in Y, so with a -5 inverse bind a bound point moves by -3.
	REQUIRE(NearlyEqual(SkinPoint(skinning, 1, glm::vec3(0.0f)), {0.0f, -3.0f, 0.0f}));
}

TEST_CASE("A clip binds to a skeleton by bone NAME, so it is an asset in its own right", "[animation]")
{
	// The workflow this exists for: a character mesh brings the skeleton, and a separate file brings the
	// walk cycle. The clip never saw this skeleton's bone ORDER, so it must bind by name.
	AnimationClip clip("Walk");
	clip.SetDuration(1.0f);
	BoneTrack& childTrack = clip.GetTrack(clip.AddTrack("Child"));
	childTrack.TranslationTimes = {0.0f, 1.0f};
	childTrack.TranslationKeys = {glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 6.0f, 0.0f)};
	// A bone that only THIS clip knows about: binding must skip it, not fall over.
	BoneTrack& strayTrack = clip.GetTrack(clip.AddTrack("Tail"));
	strayTrack.TranslationTimes = {0.0f};
	strayTrack.TranslationKeys = {glm::vec3(99.0f)};

	const Skeleton skeleton = MakeTwoBoneSkeleton();
	const std::vector<uint32_t> mapping = clip.BuildTrackToBoneMapping(skeleton);
	REQUIRE(mapping.size() == 2);
	REQUIRE(mapping[0] == skeleton.FindBoneIndex("Child"));
	REQUIRE(mapping[1] == Skeleton::NullIndex); // "Tail" is not in this skeleton

	Pose pose;
	clip.Sample(0.5f, false, skeleton, mapping, pose);
	REQUIRE(NearlyEqual(pose.BoneTransforms[1].Translation, {0.0f, 4.0f, 0.0f}));
	REQUIRE(NearlyEqual(pose.BoneTransforms[0].Translation, glm::vec3(0.0f))); // untouched root stays at rest

	// The same clip on a skeleton whose bones are in a DIFFERENT order still drives the right bone --
	// which is exactly what index-based tracks could not do.
	Skeleton reordered;
	const uint32_t hipsIndex = reordered.AddBone("Hips", Skeleton::NullIndex, {});
	BoneTransform childRest;
	childRest.Translation = {0.0f, 2.0f, 0.0f};
	const uint32_t childIndex = reordered.AddBone("Child", hipsIndex, childRest);
	reordered.Finalize();

	const std::vector<uint32_t> reorderedMapping = clip.BuildTrackToBoneMapping(reordered);
	REQUIRE(reorderedMapping[0] == childIndex);
	clip.Sample(0.5f, false, reordered, reorderedMapping, pose);
	REQUIRE(NearlyEqual(pose.BoneTransforms[childIndex].Translation, {0.0f, 4.0f, 0.0f}));
}

TEST_CASE("Posed bounds contain the skinned mesh, and never shrink below it", "[animation]")
{
	const Skeleton skeleton = MakeTwoBoneSkeleton();

	MeshBounds bind;
	bind.Box = {glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 4.0f, 1.0f)};
	bind.Sphere = {bind.Box.Center(), glm::length(bind.Box.Extents())};

	SECTION("The rest pose reproduces the bind bounds")
	{
		Pose pose;
		pose.BoneTransforms = skeleton.GetRestPose();
		std::vector<glm::mat4> skinning;
		ComputeSkinningMatrices(skeleton, pose, skinning);

		const MeshBounds posed = ComputeSkinnedBounds(bind, skinning);
		REQUIRE(NearlyEqual(posed.Box.Min, bind.Box.Min));
		REQUIRE(NearlyEqual(posed.Box.Max, bind.Box.Max));
	}

	SECTION("A rotated pose grows the box to cover where the mesh actually went")
	{
		Pose pose;
		pose.BoneTransforms = skeleton.GetRestPose();
		pose.BoneTransforms[0].Rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
		std::vector<glm::mat4> skinning;
		ComputeSkinningMatrices(skeleton, pose, skinning);

		const MeshBounds posed = ComputeSkinnedBounds(bind, skinning);

		// Rotating +Y into -X puts geometry out at x = -4, which the bind box (x >= -1) does not contain.
		REQUIRE(posed.Box.Min.x <= -4.0f + 1e-3f);

		// And it must still contain every skinned vertex: check the corners of the bind box through each
		// bone, which is what the mesh's extremes can reach.
		for (const glm::mat4& matrix : skinning)
		{
			for (int corner = 0; corner < 8; ++corner)
			{
				const glm::vec3 point{(corner & 1) ? bind.Box.Max.x : bind.Box.Min.x,
				                      (corner & 2) ? bind.Box.Max.y : bind.Box.Min.y,
				                      (corner & 4) ? bind.Box.Max.z : bind.Box.Min.z};
				const glm::vec3 skinned = glm::vec3(matrix * glm::vec4(point, 1.0f));
				REQUIRE(glm::all(glm::greaterThanEqual(skinned, posed.Box.Min - glm::vec3(1e-4f))));
				REQUIRE(glm::all(glm::lessThanEqual(skinned, posed.Box.Max + glm::vec3(1e-4f))));
			}
		}
	}

	SECTION("No matrices at all falls back to the bind bounds rather than an empty box")
	{
		const MeshBounds posed = ComputeSkinnedBounds(bind, {});
		REQUIRE(NearlyEqual(posed.Box.Min, bind.Box.Min));
		REQUIRE(NearlyEqual(posed.Box.Max, bind.Box.Max));
	}
}

TEST_CASE("Blending two poses interpolates every bone, and the ends are the inputs", "[animation][blend]")
{
	Pose a;
	a.BoneTransforms = {
	    {.Translation = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f)},
	    {.Translation = glm::vec3(0.0f, 2.0f, 0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f)},
	};
	Pose b;
	b.BoneTransforms = {
	    {.Translation = glm::vec3(4.0f, 0.0f, 0.0f), .Rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1)), .Scale = glm::vec3(3.0f)},
	    {.Translation = glm::vec3(0.0f, 6.0f, 0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f)},
	};

	Pose out;
	// w=0 and w=1 must reproduce the inputs exactly, or a finished transition would not equal the clip it
	// transitioned into -- the pose would settle a hair off and never converge.
	BlendPoses(a, b, 0.0f, out);
	REQUIRE(NearlyEqual(out.BoneTransforms[0].Translation, a.BoneTransforms[0].Translation));
	REQUIRE(NearlyEqual(out.BoneTransforms[0].Scale, a.BoneTransforms[0].Scale));
	BlendPoses(a, b, 1.0f, out);
	REQUIRE(NearlyEqual(out.BoneTransforms[0].Translation, b.BoneTransforms[0].Translation));
	REQUIRE(NearlyEqual(out.BoneTransforms[0].Scale, b.BoneTransforms[0].Scale));

	BlendPoses(a, b, 0.5f, out);
	REQUIRE(out.BoneTransforms.size() == 2);
	REQUIRE(NearlyEqual(out.BoneTransforms[0].Translation, glm::vec3(2.0f, 0.0f, 0.0f)));
	REQUIRE(NearlyEqual(out.BoneTransforms[0].Scale, glm::vec3(2.0f)));
	// Halfway between identity and a 90-degree turn is 45 degrees: rotate +X and check where it lands.
	const glm::vec3 rotated = out.BoneTransforms[0].Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
	REQUIRE(NearlyEqual(rotated, glm::vec3(glm::root_two<float>() * 0.5f, glm::root_two<float>() * 0.5f, 0.0f)));
	// A bone both poses agree on must come out untouched at any weight.
	REQUIRE(NearlyEqual(out.BoneTransforms[1].Translation, glm::vec3(0.0f, 4.0f, 0.0f)));
}

TEST_CASE("A blend takes the shortest arc between rotations", "[animation][blend]")
{
	// q and -q are the SAME rotation. Blending towards the negated form must still travel the short way;
	// a naive slerp on the raw pair sweeps ~350 degrees instead of ~10 and the limb visibly spins.
	const glm::quat start = glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 0, 1));
	const glm::quat endNegated = -glm::angleAxis(glm::radians(20.0f), glm::vec3(0, 0, 1));

	Pose a;
	a.BoneTransforms = {{.Translation = glm::vec3(0.0f), .Rotation = start, .Scale = glm::vec3(1.0f)}};
	Pose b;
	b.BoneTransforms = {{.Translation = glm::vec3(0.0f), .Rotation = endNegated, .Scale = glm::vec3(1.0f)}};

	Pose out;
	BlendPoses(a, b, 0.5f, out);

	// Halfway between 10 and 20 degrees is 15 -- NOT somewhere out on the far side of the circle.
	const glm::vec3 rotated = out.BoneTransforms[0].Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
	const glm::vec3 expected = glm::angleAxis(glm::radians(15.0f), glm::vec3(0, 0, 1)) * glm::vec3(1.0f, 0.0f, 0.0f);
	REQUIRE(NearlyEqual(rotated, expected));
}
