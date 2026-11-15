#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

struct ma_engine;
struct ma_sound;

namespace Bellows {

	class Scene;
	class Entity;

	// Process-wide audio output (miniaudio). Falls back to a silent null device when no audio
	// hardware is available, so headless servers and CI behave like a normal machine.
	class AudioEngine
	{
	public:
		static bool Init(bool forceNullDevice = false);
		static void Shutdown();
		static bool IsInitialized();
		static bool IsNullDevice();

		static ma_engine* GetEngine();

		static void SetMasterVolume(float volume);
		static float GetMasterVolume();

		// Fire-and-forget 2D sound (UI clicks, pickups). Path is absolute or project-relative.
		static bool PlayOneShot(const std::string& path, float volume = 1.0f);

		static void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
	};

	// Sounds owned by the AudioSource components of one running scene.
	class SceneAudio
	{
	public:
		explicit SceneAudio(Scene& scene);
		~SceneAudio();

		SceneAudio(const SceneAudio&) = delete;
		SceneAudio& operator=(const SceneAudio&) = delete;

		void OnRuntimeStart();
		// Creates sounds for new sources, follows entity positions and the active listener.
		void OnUpdate();
		void OnEntityDestroyed(Entity entity);

		bool Play(Entity entity);
		void Stop(Entity entity);
		void Pause(Entity entity);
		bool IsPlaying(Entity entity) const;
		// Applies volume/pitch/loop/distance settings from the component to a live sound.
		void ApplySettings(Entity entity);

		size_t GetSoundCount() const { return m_Sounds.size(); }
	private:
		ma_sound* GetOrCreateSound(Entity entity);
	private:
		Scene& m_Scene;
		std::unordered_map<UUID, ma_sound*> m_Sounds;
		std::unordered_map<UUID, std::string> m_SoundPaths;
	};

}
