#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct ma_engine;
struct ma_sound;
typedef struct ma_sound ma_sound_group;

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

		// Buses group sounds so their volume can be set together, like a mixer: "Master" (everything),
		// "Music" and "Effects" (both feed Master). Unknown names are rejected (false / 0).
		static const std::vector<std::string>& GetBusNames();
		static bool IsBus(const std::string& bus);
		static bool SetBusVolume(const std::string& bus, float volume);
		static float GetBusVolume(const std::string& bus);
		// Muting keeps the volume, so unmuting restores it.
		static bool SetBusMuted(const std::string& bus, bool muted);
		static bool IsBusMuted(const std::string& bus);
		// The miniaudio group sounds of a bus are created in; null for Master (the engine itself).
		static ma_sound_group* GetBusGroup(const std::string& bus);

		// Fire-and-forget 2D sound (UI clicks, pickups). Path is absolute or project-relative.
		static bool PlayOneShot(const std::string& path, float volume = 1.0f, const std::string& bus = "Effects");

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
		std::unordered_map<UUID, std::string> m_SoundKeys;	// Clip and bus a sound was made with
	};

}
