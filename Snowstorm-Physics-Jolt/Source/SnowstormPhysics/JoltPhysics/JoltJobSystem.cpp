#include "JoltJobSystem.hpp"

#include <Snowstorm/Core/JobSystem.hpp>

namespace Snowstorm
{
	JoltJobSystem::JoltJobSystem(Snowstorm::JobSystem& jobs, const int maxBarriers)
	    : m_Jobs(jobs)
	{
		JobSystemWithBarrier::Init(static_cast<JPH::uint>(maxBarriers));
	}

	int JoltJobSystem::GetMaxConcurrency() const
	{
		return static_cast<int>(m_Jobs.WorkerCount()) + 1; // + the thread that waits on the barrier
	}

	JPH::JobSystem::JobHandle JoltJobSystem::CreateJob(const char* name, const JPH::ColorArg color, const JobFunction& function, const JPH::uint32 numDependencies)
	{
		// Heap allocation per job (Jolt's own pool uses a fixed-size free list; a few hundred jobs per step
		// at thesis scene sizes don't justify one). FreeJob deletes it when the last reference drops.
		Job* job = new Job(name, color, this, function, numDependencies);
		JobHandle handle(job); // AddRef for the caller's handle
		if (numDependencies == 0)
		{
			QueueJob(job);
		}
		return handle;
	}

	void JoltJobSystem::QueueJob(Job* job)
	{
		// Keep the job alive until our pool has executed it (the Jolt contract: QueueJob takes a reference).
		job->AddRef();
		(void)m_Jobs.Submit([job]()
		                    {
			job->Execute();
			job->Release(); });
	}

	void JoltJobSystem::QueueJobs(Job** jobs, const JPH::uint numJobs)
	{
		for (JPH::uint i = 0; i < numJobs; ++i)
		{
			QueueJob(jobs[i]);
		}
	}

	void JoltJobSystem::FreeJob(Job* job)
	{
		delete job;
	}
}
