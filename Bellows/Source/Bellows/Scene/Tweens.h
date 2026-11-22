#pragma once

#include "Bellows/Core/UUID.h"
#include "Bellows/Math/Easing.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	class Entity;
	class Scene;

	// How many numbers a tweened property holds: a number, or a Vec2, Vec3 or Vec4.
	enum class TweenValueType : uint8_t { Number = 1, Vec2 = 2, Vec3 = 3, Vec4 = 4 };

	// Euler-angle properties turn along the shortest way (a quaternion slerp) instead of moving each
	// angle on its own; the units say how to read them.
	enum class TweenRotation : uint8_t { None, Radians, Degrees };

	// A property a tween can move, read and written as a vec4 (unused parts are 0). Get and Set may
	// throw when the entity no longer has what the property needs; the tween then ends quietly.
	struct TweenProperty
	{
		std::string Name;	// As scripts write it: "position", "UIImage.color"
		TweenValueType Type = TweenValueType::Number;
		TweenRotation Rotation = TweenRotation::None;
		std::function<glm::vec4(Entity)> Get;
		std::function<void(Entity, const glm::vec4&)> Set;
	};

	struct TweenSettings
	{
		const TweenProperty* Property = nullptr;
		glm::vec4 Value { 0.0f };	// Where it goes, or how far with Relative
		bool Relative = false;
		double Duration = 0.0;		// Seconds
		double Delay = 0.0;			// Seconds before it starts (and reads its starting value)
		Math::EaseCurve Ease;
	};

	// A tween that ran to its end, for the 'tween finished' event.
	struct FinishedTween
	{
		uint64_t ID = 0;
		UUID Entity = 0;
		std::string Property;
	};

	// The running tweens of a scene. They are play-time state only: never saved or copied with the scene.
	class SceneTweens
	{
	public:
		static constexpr size_t c_MaxTweens = 10000;
		static constexpr double c_MaxSeconds = 1000000.0;
		static constexpr int64_t c_MaxRepeat = 1000000;

		// Starts a tween and returns its ID (from 1). A running tween on the same entity and property is
		// stopped: the new one starts from wherever that one got to. Throws std::length_error when the
		// scene already has c_MaxTweens running.
		uint64_t Start(UUID entity, const TweenSettings& settings);
		// Stops an entity's tweens, or only those of one property; returns how many it stopped.
		size_t StopAll(UUID entity, std::string_view property = {});

		// What a handle can ask and change; each is false or 0 for a tween that has ended.
		bool IsRunning(uint64_t id) const;
		bool HasFinished(uint64_t id) const;	// Ran to its end (it was not stopped)
		double GetProgress(uint64_t id) const;	// 0 to 1 over every repeat (the current one when repeating forever); 1 once finished
		UUID GetEntity(uint64_t id) const;
		const TweenProperty* GetProperty(uint64_t id) const;
		int64_t GetRepeat(uint64_t id) const;
		bool GetYoyo(uint64_t id) const;
		bool GetWhilePaused(uint64_t id) const;
		void SetRepeat(uint64_t id, int64_t repeat);
		void SetYoyo(uint64_t id, bool yoyo);
		void SetWhilePaused(uint64_t id, bool whilePaused);
		bool Stop(uint64_t id);
		bool Pause(uint64_t id);
		bool Resume(uint64_t id);
		// Sets the final value at once, as if the tween had run to its end (repeats included); its
		// 'tween finished' goes out with the next update's.
		bool Complete(Scene& scene, uint64_t id);

		// Moves every running tween on by 'seconds'. While the game is paused only tweens marked
		// WhilePaused move. Returns the tweens that ran to their end, in the order they finished.
		std::vector<FinishedTween> Update(Scene& scene, double seconds, bool gamePaused);
		size_t GetCount() const { return m_Tweens.size(); }
		void Clear();
	private:
		struct Tween
		{
			uint64_t ID = 0;
			UUID Entity = 0;
			TweenSettings Settings;
			int64_t Repeat = 0;			// Extra runs after the first; -1 forever
			bool Yoyo = false;
			bool WhilePaused = false;
			bool Paused = false;
			bool Started = false;		// The delay has passed and the start value is read
			bool Ended = false;			// Removed at the end of the update
			double Elapsed = 0.0;		// Seconds since the delay ended
			double DelayLeft = 0.0;
			glm::vec4 From { 0.0f };
			glm::vec4 To { 0.0f };
		};

		Tween* Find(uint64_t id);
		const Tween* Find(uint64_t id) const;
		// Applies the tween at 'elapsed' seconds; false when the entity can no longer take it.
		static bool Apply(Entity entity, const Tween& tween, double elapsed);
		static bool Begin(Entity entity, Tween& tween);
		static double TotalSeconds(const Tween& tween);
		void MarkEnded(uint64_t id, bool finished);
	private:
		std::vector<Tween> m_Tweens;
		std::vector<FinishedTween> m_PendingFinished;	// Completed by a script since the last update
		uint64_t m_NextID = 1;
		// One bit per tween ever started, set when it ran to its end, so a handle's 'finished' stays
		// right for the whole of play (a million tweens take 125 KB).
		std::vector<bool> m_Finished;
	};

}
