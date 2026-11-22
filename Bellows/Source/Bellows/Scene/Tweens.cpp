#include "bwpch.h"
#include "Bellows/Scene/Tweens.h"

#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Bellows {

	namespace {

		glm::quat ToQuaternion(const glm::vec4& euler, TweenRotation units)
		{
			const glm::vec3 angles(euler);
			return glm::quat(units == TweenRotation::Degrees ? glm::radians(angles) : angles);
		}

		glm::vec4 ToEuler(const glm::quat& rotation, TweenRotation units)
		{
			const glm::vec3 angles = glm::eulerAngles(rotation);
			return glm::vec4(units == TweenRotation::Degrees ? glm::degrees(angles) : angles, 0.0f);
		}

	}

	uint64_t SceneTweens::Start(UUID entity, const TweenSettings& settings)
	{
		StopAll(entity, settings.Property->Name);
		if (m_Tweens.size() >= c_MaxTweens)
			throw std::length_error("A scene can have " + std::to_string(c_MaxTweens) + " tweens running at once");

		Tween tween;
		tween.ID = m_NextID++;
		tween.Entity = entity;
		tween.Settings = settings;
		tween.DelayLeft = settings.Delay;
		m_Tweens.push_back(tween);
		m_Finished.push_back(false);
		return tween.ID;
	}

	size_t SceneTweens::StopAll(UUID entity, std::string_view property)
	{
		const auto first = std::stable_partition(m_Tweens.begin(), m_Tweens.end(), [&](const Tween& tween)
		{
			return !(tween.Entity == entity && (property.empty() || tween.Settings.Property->Name == property));
		});
		const size_t stopped = static_cast<size_t>(m_Tweens.end() - first);
		m_Tweens.erase(first, m_Tweens.end());
		return stopped;
	}

	SceneTweens::Tween* SceneTweens::Find(uint64_t id)
	{
		const auto it = std::find_if(m_Tweens.begin(), m_Tweens.end(), [id](const Tween& tween) { return tween.ID == id; });
		return it == m_Tweens.end() ? nullptr : &*it;
	}

	const SceneTweens::Tween* SceneTweens::Find(uint64_t id) const
	{
		return const_cast<SceneTweens*>(this)->Find(id);
	}

	bool SceneTweens::IsRunning(uint64_t id) const { return Find(id) != nullptr; }

	bool SceneTweens::HasFinished(uint64_t id) const
	{
		return id >= 1 && id < m_NextID && m_Finished[id - 1];
	}

	double SceneTweens::GetProgress(uint64_t id) const
	{
		const Tween* tween = Find(id);
		if (!tween)
			return HasFinished(id) ? 1.0 : 0.0;
		if (!tween->Started || tween->Settings.Duration <= 0.0)
			return 0.0;
		if (tween->Repeat < 0)
			return std::fmod(tween->Elapsed, tween->Settings.Duration) / tween->Settings.Duration;
		return std::clamp(tween->Elapsed / TotalSeconds(*tween), 0.0, 1.0);
	}

	UUID SceneTweens::GetEntity(uint64_t id) const
	{
		const Tween* tween = Find(id);
		return tween ? tween->Entity : UUID(0);
	}

	const TweenProperty* SceneTweens::GetProperty(uint64_t id) const
	{
		const Tween* tween = Find(id);
		return tween ? tween->Settings.Property : nullptr;
	}

	int64_t SceneTweens::GetRepeat(uint64_t id) const { const Tween* tween = Find(id); return tween ? tween->Repeat : 0; }
	bool SceneTweens::GetYoyo(uint64_t id) const { const Tween* tween = Find(id); return tween && tween->Yoyo; }
	bool SceneTweens::GetWhilePaused(uint64_t id) const { const Tween* tween = Find(id); return tween && tween->WhilePaused; }
	void SceneTweens::SetRepeat(uint64_t id, int64_t repeat) { if (Tween* tween = Find(id)) tween->Repeat = std::clamp<int64_t>(repeat, -1, c_MaxRepeat); }
	void SceneTweens::SetYoyo(uint64_t id, bool yoyo) { if (Tween* tween = Find(id)) tween->Yoyo = yoyo; }
	void SceneTweens::SetWhilePaused(uint64_t id, bool whilePaused) { if (Tween* tween = Find(id)) tween->WhilePaused = whilePaused; }

	bool SceneTweens::Stop(uint64_t id)
	{
		const auto it = std::find_if(m_Tweens.begin(), m_Tweens.end(), [id](const Tween& tween) { return tween.ID == id; });
		if (it == m_Tweens.end())
			return false;
		m_Tweens.erase(it);
		return true;
	}

	bool SceneTweens::Pause(uint64_t id)
	{
		Tween* tween = Find(id);
		if (tween)
			tween->Paused = true;
		return tween != nullptr;
	}

	bool SceneTweens::Resume(uint64_t id)
	{
		Tween* tween = Find(id);
		if (tween)
			tween->Paused = false;
		return tween != nullptr;
	}

	bool SceneTweens::Complete(Scene& scene, uint64_t id)
	{
		const auto it = std::find_if(m_Tweens.begin(), m_Tweens.end(), [id](const Tween& tween) { return tween.ID == id; });
		if (it == m_Tweens.end())
			return false;
		Tween tween = *it;
		m_Tweens.erase(it);
		// A tween repeating forever has no end; completing it finishes the run it is on.
		if (tween.Repeat < 0)
			tween.Repeat = 0;
		Entity entity = scene.TryGetEntityByUUID(tween.Entity);
		if (entity && (tween.Started || Begin(entity, tween)) && Apply(entity, tween, TotalSeconds(tween)))
		{
			MarkEnded(tween.ID, true);
			// Sent with the next update's, not from inside the script call that completed it.
			m_PendingFinished.push_back({ tween.ID, tween.Entity, tween.Settings.Property->Name });
		}
		return true;
	}

	double SceneTweens::TotalSeconds(const Tween& tween)
	{
		// A tween of no length ends at once, even when it repeats forever.
		if (tween.Settings.Duration <= 0.0)
			return 0.0;
		if (tween.Repeat < 0)
			return std::numeric_limits<double>::infinity();
		return tween.Settings.Duration * static_cast<double>(tween.Repeat + 1);
	}

	bool SceneTweens::Begin(Entity entity, Tween& tween)
	{
		const TweenSettings& settings = tween.Settings;
		try
		{
			tween.From = settings.Property->Get(entity);
		}
		catch (const std::exception&)
		{
			return false;
		}
		// A rotation by an amount keeps the amount (it can be several turns); everything else
		// keeps where it ends.
		if (settings.Property->Rotation != TweenRotation::None)
			tween.To = settings.Value;
		else
			tween.To = settings.Relative ? tween.From + settings.Value : settings.Value;
		tween.Started = true;
		return true;
	}

	bool SceneTweens::Apply(Entity entity, const Tween& tween, double elapsed)
	{
		const TweenSettings& settings = tween.Settings;
		const double duration = settings.Duration;

		// Which run this is and how far through it: the final value once every run is over.
		double t = 1.0;
		int64_t run = tween.Repeat < 0 ? 0 : tween.Repeat;
		if (duration > 0.0 && elapsed < TotalSeconds(tween))
		{
			const double runs = std::floor(elapsed / duration);
			run = static_cast<int64_t>(runs);
			t = (elapsed - runs * duration) / duration;
		}
		if (tween.Yoyo && run % 2 == 1)
			t = 1.0 - t;
		const bool atEnd = t >= 1.0;
		const float eased = atEnd ? 1.0f : static_cast<float>(settings.Ease(t));
		const bool atStart = t <= 0.0;

		glm::vec4 value;
		const TweenRotation rotation = settings.Property->Rotation;
		if (rotation == TweenRotation::None)
			value = tween.From + (tween.To - tween.From) * (atStart ? 0.0f : eased);
		else if (atStart)
			value = tween.From;
		else if (atEnd)
			// The angles asked for, not an equal turn written another way.
			value = settings.Relative ? tween.From + tween.To : tween.To;
		else if (settings.Relative)
			value = ToEuler(ToQuaternion(tween.From, rotation) * ToQuaternion(tween.To * eased, rotation), rotation);
		else
			value = ToEuler(glm::slerp(ToQuaternion(tween.From, rotation), ToQuaternion(tween.To, rotation), eased), rotation);

		try
		{
			settings.Property->Set(entity, value);
		}
		catch (const std::exception&)
		{
			return false;
		}
		return true;
	}

	void SceneTweens::MarkEnded(uint64_t id, bool finished)
	{
		if (id >= 1 && id < m_NextID)
			m_Finished[id - 1] = finished;
	}

	std::vector<FinishedTween> SceneTweens::Update(Scene& scene, double seconds, bool gamePaused)
	{
		std::vector<FinishedTween> finished = std::move(m_PendingFinished);
		m_PendingFinished.clear();
		// Setters only write values, so nothing here starts or stops tweens while the loop runs.
		bool anyEnded = false;
		const auto end = [&](Tween& tween)
		{
			tween.Ended = true;
			anyEnded = true;
		};
		for (Tween& tween : m_Tweens)
		{
			if (tween.Paused || (gamePaused && !tween.WhilePaused))
				continue;
			Entity entity = scene.TryGetEntityByUUID(tween.Entity);
			if (!entity)
			{
				end(tween);
				continue;
			}

			double step = seconds;
			if (!tween.Started)
			{
				if (tween.DelayLeft > step)
				{
					tween.DelayLeft -= step;
					continue;
				}
				step -= tween.DelayLeft;
				tween.DelayLeft = 0.0;
				if (!Begin(entity, tween))
				{
					end(tween);
					continue;
				}
			}

			tween.Elapsed += step;
			if (!Apply(entity, tween, tween.Elapsed))
			{
				end(tween);
				continue;
			}
			if (tween.Elapsed >= TotalSeconds(tween))
			{
				end(tween);
				MarkEnded(tween.ID, true);
				finished.push_back({ tween.ID, tween.Entity, tween.Settings.Property->Name });
			}
		}
		if (anyEnded)
			std::erase_if(m_Tweens, [](const Tween& tween) { return tween.Ended; });
		return finished;
	}

	void SceneTweens::Clear()
	{
		m_Tweens.clear();
		m_PendingFinished.clear();
	}

}
