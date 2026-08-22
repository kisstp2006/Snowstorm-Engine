#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemWithBarrier.h>

#include <Snowstorm/Service/Service.hpp>

namespace Snowstorm
{
	class JobSystem;

	// Jolt job system backed by the engine's JobSystem (one worker pool for the whole engine, like
	// ezEngine's ezJoltJobSystem on ezTaskSystem): Jolt jobs are heap-allocated and submitted to our pool;
	// JobSystemWithBarrier provides the barrier/wait logic (the waiting thread helps execute).
	class JoltJobSystem final : public Service, public JPH::JobSystemWithBarrier
	{
	public:
		explicit JoltJobSystem(Snowstorm::JobSystem& jobs, int maxBarriers = 8);

		[[nodiscard]] int GetMaxConcurrency() const override;
		JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function, JPH::uint32 numDependencies = 0) override;

	protected:
		void QueueJob(Job* job) override;
		void QueueJobs(Job** jobs, JPH::uint numJobs) override;
		void FreeJob(Job* job) override;

	private:
		Snowstorm::JobSystem& m_Jobs;
	};
}
