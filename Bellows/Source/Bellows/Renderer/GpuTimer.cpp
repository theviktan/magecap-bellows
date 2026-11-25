#include "bwpch.h"
#include "Bellows/Renderer/GpuTimer.h"

#include "Bellows/Core/Profiler.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"

namespace Bellows {

	namespace {

		// NVRHI's pool holds 256 timer queries and reports an error when it runs out; every GpuTimer
		// together stays well under that, and a pass past it simply isn't timed.
		constexpr size_t c_MaxQueries = 192;
		size_t s_QueryCount = 0;

		constexpr size_t c_Untimed = SIZE_MAX;

	}

	GpuTimer::GpuTimer(const char* name)
		: m_Name(name ? name : "")
	{
	}

	GpuTimer::~GpuTimer()
	{
		bool queriesInFlight = false;
		size_t queryCount = 0;
		for (const Recording& recording : m_Recordings)
		{
			queriesInFlight |= recording.Used > 0;
			queryCount += recording.Timings.size();
		}
		// NVRHI frees a query's place in the pool at once; one the GPU may still write must not be
		// handed to another timer first.
		if (queriesInFlight && Renderer::IsInitialized())
			Renderer::GetDevice()->waitForIdle();
		s_QueryCount -= std::min(s_QueryCount, queryCount);
	}

	bool GpuTimer::IsSupported()
	{
		return Renderer::IsInitialized() && Renderer::GetRenderDevice().SupportsTimestamps();
	}

	void GpuTimer::Begin(nvrhi::ICommandList* commandList)
	{
		m_Recording = false;
		m_Open.clear();
		if (!m_Enabled || !IsSupported())
			return;

		Recording& recording = m_Recordings[m_Current];
		Collect(recording);
		// Times are only kept for a profiled frame, so outside one nothing is recorded.
		recording.Frame = Profiler::GetRecordingFrame();
		if (!recording.Frame)
			return;

		m_Recording = true;
		BeginScope(commandList, m_Name);
	}

	void GpuTimer::BeginScope(nvrhi::ICommandList* commandList, const char* name)
	{
		if (!m_Recording)
			return;

		Recording& recording = m_Recordings[m_Current];
		if (recording.Used == recording.Timings.size())
		{
			if (s_QueryCount >= c_MaxQueries)
			{
				m_Open.push_back(c_Untimed);
				return;
			}
			Timing timing;
			timing.Query = Renderer::GetDevice()->createTimerQuery();
			if (!timing.Query)
			{
				m_Open.push_back(c_Untimed);
				return;
			}
			s_QueryCount++;
			recording.Timings.push_back(std::move(timing));
		}

		Timing& timing = recording.Timings[recording.Used];
		timing.Name = name ? name : "";
		timing.Depth = static_cast<uint32_t>(m_Open.size());
		commandList->beginTimerQuery(timing.Query);
		m_Open.push_back(recording.Used);
		recording.Used++;
	}

	void GpuTimer::EndScope(nvrhi::ICommandList* commandList)
	{
		if (!m_Recording || m_Open.empty())
			return;

		const size_t index = m_Open.back();
		m_Open.pop_back();
		if (index != c_Untimed)
			commandList->endTimerQuery(m_Recordings[m_Current].Timings[index].Query);
	}

	void GpuTimer::End(nvrhi::ICommandList* commandList)
	{
		if (!m_Recording)
			return;

		while (!m_Open.empty())
			EndScope(commandList);
		m_Recording = false;
		m_Current = (m_Current + 1) % m_Recordings.size();
	}

	void GpuTimer::Collect(Recording& recording)
	{
		if (recording.Used == 0)
			return;

		nvrhi::IDevice* device = Renderer::GetDevice();
		// Only read times the GPU has written: getTimerQueryTime would wait for the rest. A recording
		// that isn't done when its slot comes round (rendered more often than frames) is dropped.
		bool ready = true;
		for (size_t i = 0; i < recording.Used && ready; i++)
			ready = device->pollTimerQuery(recording.Timings[i].Query);

		std::vector<ProfileSample> samples;
		if (ready)
			samples.reserve(recording.Used);
		for (size_t i = 0; i < recording.Used; i++)
		{
			Timing& timing = recording.Timings[i];
			if (ready)
			{
				ProfileSample sample;
				sample.Name = timing.Name;
				sample.Depth = timing.Depth;
				sample.DurationMs = static_cast<double>(device->getTimerQueryTime(timing.Query)) * 1000.0;
				samples.push_back(sample);
			}
			device->resetTimerQuery(timing.Query);
		}
		recording.Used = 0;

		if (ready && recording.Frame)
			Profiler::AddGpuSamples(*recording.Frame, samples);
	}

}
