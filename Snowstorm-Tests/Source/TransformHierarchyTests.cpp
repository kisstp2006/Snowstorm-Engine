#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/HierarchyComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/WorldTransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Math/Transform.hpp"
#include "Snowstorm/Systems/TransformSystem.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"
#include "Snowstorm/World/World.hpp"

#include <nlohmann/json.hpp>

using namespace Snowstorm;

namespace
{
	bool MatNear(const glm::mat4& a, const glm::mat4& b, const float eps = 1e-4f)
	{
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (std::abs(a[c][r] - b[c][r]) > eps)
					return false;
		return true;
	}

	Entity MakeTransformEntity(World& world, const char* name, const glm::vec3 pos, const glm::quat rot = glm::quat(1, 0, 0, 0), const glm::vec3 scale = glm::vec3(1.0f))
	{
		Entity e = world.CreateEntity(name);
		auto& tr = e.AddComponent<TransformComponent>();
		tr.Position = pos;
		tr.Rotation = rot;
		tr.Scale = scale;
		return e;
	}

	// Tests run without an Application/JobSystem: force the serial path (same as SystemParallelForEachTests).
	struct ScopedSerialEcs
	{
		bool Prev = CVars::EcsParallel.Get();
		ScopedSerialEcs() { CVars::EcsParallel.Set(false); }
		~ScopedSerialEcs() { CVars::EcsParallel.Set(Prev); }
	};

	uint32_t DepthOf(World& world, const Entity e)
	{
		const auto* h = world.GetRegistry().try_get_const<HierarchyComponent>(e.Handle());
		return h ? h->Depth : 0u;
	}
}

TEST_CASE("SetParent keeps the world transform and updates depth", "[hierarchy]")
{
	World world;
	Entity parent = MakeTransformEntity(world, "P", {10, 0, 0}, QuatFromEulerDegrees({0, 90, 0}), {2, 2, 2});
	Entity child = MakeTransformEntity(world, "C", {1, 2, 3}, QuatFromEulerDegrees({30, 0, 0}));

	const glm::mat4 childWorldBefore = world.ComputeWorldMatrix(child);
	REQUIRE(world.SetParent(child, parent, /*keepWorld*/ true));

	REQUIRE(world.GetParent(child) == parent);
	REQUIRE(DepthOf(world, child) == 1);
	REQUIRE(DepthOf(world, parent) == 0);
	REQUIRE(MatNear(world.ComputeWorldMatrix(child), childWorldBefore));

	// Local values were rewritten relative to the parent.
	REQUIRE(child.GetComponent<TransformComponent>().Position != glm::vec3(1, 2, 3));

	// Unparent (keep world): back at root with the same world pose.
	REQUIRE(world.SetParent(child, Entity{}, true));
	REQUIRE_FALSE(world.GetParent(child));
	REQUIRE(DepthOf(world, child) == 0);
	REQUIRE(MatNear(world.ComputeWorldMatrix(child), childWorldBefore));
}

TEST_CASE("SetParent rejects self-parenting and cycles", "[hierarchy]")
{
	World world;
	Entity a = MakeTransformEntity(world, "A", {0, 0, 0});
	Entity b = MakeTransformEntity(world, "B", {0, 0, 0});
	Entity c = MakeTransformEntity(world, "C", {0, 0, 0});
	REQUIRE(world.SetParent(b, a));
	REQUIRE(world.SetParent(c, b));

	REQUIRE_FALSE(world.SetParent(a, a));
	REQUIRE_FALSE(world.SetParent(a, c)); // c is a's grandchild
	REQUIRE(world.IsDescendantOf(c, a));
	REQUIRE(DepthOf(world, c) == 2);

	// Sibling order is insertion order.
	Entity d = MakeTransformEntity(world, "D", {0, 0, 0});
	REQUIRE(world.SetParent(d, a));
	std::vector<std::string> names;
	world.ForEachChild(a, [&](const Entity e)
	                   { names.push_back(e.GetComponent<TagComponent>().Tag); });
	REQUIRE(names == std::vector<std::string>{"B", "D"});
}

TEST_CASE("Destroying a parent destroys its subtree", "[hierarchy]")
{
	World world;
	Entity a = MakeTransformEntity(world, "A", {0, 0, 0});
	Entity b = MakeTransformEntity(world, "B", {0, 0, 0});
	Entity c = MakeTransformEntity(world, "C", {0, 0, 0});
	Entity other = MakeTransformEntity(world, "Other", {0, 0, 0});
	world.SetParent(b, a);
	world.SetParent(c, b);
	world.SetParent(other, a);

	// Detach `other` so it must survive; destroy `a` -> b and c go with it.
	world.SetParent(other, Entity{});
	world.DestroyEntity(a);
	world.FlushDestroyQueue();

	REQUIRE_FALSE(a.IsValid());
	REQUIRE_FALSE(b.IsValid());
	REQUIRE_FALSE(c.IsValid());
	REQUIRE(other.IsValid());
	REQUIRE_FALSE(world.GetParent(other));
}

TEST_CASE("TransformSystem propagates parent motion into children's world matrices", "[hierarchy][ecs]")
{
	ScopedSerialEcs serial;
	World world;
	TransformSystem system(&world);

	Entity parent = MakeTransformEntity(world, "P", {0, 0, 0});
	Entity child = MakeTransformEntity(world, "C", {1, 0, 0});
	world.SetParent(child, parent, false);

	system.Execute(Timestep{0.016f});
	auto& reg = world.GetRegistry();
	REQUIRE(glm::vec3(reg.Read<WorldTransformComponent>(child.Handle()).LocalToWorld[3]) == glm::vec3(1, 0, 0));

	// Move the parent only: the child's world matrix follows and is marked changed; the parent's too.
	reg.ClearTrackedComponents();
	reg.Write<TransformComponent>(parent.Handle()).Position = {5, 0, 0};
	system.Execute(Timestep{0.016f});
	REQUIRE(glm::vec3(reg.Read<WorldTransformComponent>(child.Handle()).LocalToWorld[3]) == glm::vec3(6, 0, 0));
	REQUIRE(reg.WasChanged<WorldTransformComponent>(child.Handle()));
	REQUIRE(reg.WasChanged<WorldTransformComponent>(parent.Handle()));

	// Nothing moved: nothing is marked.
	reg.ClearTrackedComponents();
	system.Execute(Timestep{0.016f});
	REQUIRE_FALSE(reg.WasChanged<WorldTransformComponent>(child.Handle()));
	REQUIRE_FALSE(reg.WasChanged<WorldTransformComponent>(parent.Handle()));
}

TEST_CASE("SceneSerializer round-trips the hierarchy (child listed before its parent)", "[hierarchy][serialize]")
{
	World src;
	Entity parent = MakeTransformEntity(src, "Parent", {1, 0, 0});
	Entity child = MakeTransformEntity(src, "Child", {0, 2, 0}, QuatFromEulerDegrees({0, 45, 0}));
	Entity grandchild = MakeTransformEntity(src, "Grandchild", {0, 0, 3});
	src.SetParent(child, parent, false);
	src.SetParent(grandchild, child, false);
	const glm::mat4 worldGC = src.ComputeWorldMatrix(grandchild);

	nlohmann::json root = nlohmann::json::parse(SceneSerializer::SerializeToString(src));
	// Saved parent-first, depth-first.
	REQUIRE(root["Entities"][0]["Name"] == "Parent");
	REQUIRE(root["Entities"][1]["Name"] == "Child");
	REQUIRE(root["Entities"][1]["Parent"] == parent.GetComponent<IDComponent>().Id.ToString());
	REQUIRE(root["Entities"][2]["Name"] == "Grandchild");
	REQUIRE_FALSE(root["Entities"][0].contains("Parent"));

	// Reverse the file order so every child precedes its parent: the second pass must still link them.
	std::reverse(root["Entities"].begin(), root["Entities"].end());

	World dst;
	REQUIRE(SceneSerializer::DeserializeFromString(dst, root.dump()));
	const Entity gc = dst.FindEntityByUUID(grandchild.GetComponent<IDComponent>().Id);
	const Entity c = dst.FindEntityByUUID(child.GetComponent<IDComponent>().Id);
	const Entity p = dst.FindEntityByUUID(parent.GetComponent<IDComponent>().Id);
	REQUIRE(dst.GetParent(gc) == c);
	REQUIRE(dst.GetParent(c) == p);
	REQUIRE_FALSE(dst.GetParent(p));
	REQUIRE(DepthOf(dst, gc) == 2);
	REQUIRE(MatNear(dst.ComputeWorldMatrix(gc), worldGC));
}

TEST_CASE("Quaternion rotation survives a JSON round-trip", "[serialize]")
{
	World src;
	Entity e = MakeTransformEntity(src, "Q", {0, 0, 0}, QuatFromEulerDegrees({10, 20, 30}));
	nlohmann::json snap;
	REQUIRE(SceneSerializer::SerializeEntity(e, snap));
	REQUIRE(snap["Components"]["Snowstorm::TransformComponent"]["Rotation"].size() == 4);

	World dst;
	const Entity r = SceneSerializer::DeserializeEntity(dst, snap);
	const glm::quat q = r.GetComponent<TransformComponent>().Rotation;
	const glm::vec3 deg = EulerDegreesFromQuat(q);
	REQUIRE(deg.x == Catch::Approx(10.0f).margin(1e-3f));
	REQUIRE(deg.y == Catch::Approx(20.0f).margin(1e-3f));
	REQUIRE(deg.z == Catch::Approx(30.0f).margin(1e-3f));
}
