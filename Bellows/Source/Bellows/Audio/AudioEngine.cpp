#include "bwpch.h"
#include "Bellows/Audio/AudioEngine.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <miniaudio.h>

namespace Bellows {

	namespace {

		struct Bus
		{
			std::string Name;
			ma_sound_group Group {};		// Unused for Master, which is the engine itself
			bool GroupReady = false;		// False if the group failed to start: its sounds go straight to Master
			float Volume = 1.0f;
			bool Muted = false;
		};

		struct AudioState
		{
			ma_engine Engine {};
			bool Initialized = false;
			bool NullDevice = false;
			std::vector<ma_sound*> OneShots;	// Fire-and-forget sounds, released once finished
			Bus Buses[3] = { { "Master" }, { "Music" }, { "Effects" } };
		};


		AudioState s_Audio;

		void ReleaseOneShots(bool releaseAll)
		{
			std::erase_if(s_Audio.OneShots, [releaseAll](ma_sound* sound)
			{
				if (!releaseAll && !ma_sound_at_end(sound))
					return false;
				ma_sound_uninit(sound);
				delete sound;
				return true;
			});
		}

		std::filesystem::path ResolveAudioPath(const std::string& path)
		{
			return AssetManager::GetAbsolutePath(path);
		}

		Bus* FindBus(const std::string& name)
		{
			for (Bus& bus : s_Audio.Buses)
			{
				if (bus.Name == name)
					return &bus;
			}
			return nullptr;
		}

		bool IsMaster(const Bus& bus)
		{
			return &bus == &s_Audio.Buses[0];
		}

		void ApplyBusVolume(Bus& bus)
		{
			if (!s_Audio.Initialized)
				return;
			const float volume = bus.Muted ? 0.0f : bus.Volume;
			if (IsMaster(bus))
				ma_engine_set_volume(&s_Audio.Engine, volume);
			else if (bus.GroupReady)
				ma_sound_group_set_volume(&bus.Group, volume);
		}

		void InitBuses()
		{
			for (Bus& bus : s_Audio.Buses)
			{
				bus.Volume = 1.0f;
				bus.Muted = false;
				if (IsMaster(bus))
					continue;
				bus.GroupReady = ma_sound_group_init(&s_Audio.Engine, 0, nullptr, &bus.Group) == MA_SUCCESS;
				if (!bus.GroupReady)
					BW_CORE_ERROR("Audio: could not create the '{}' bus; its sounds play on Master", bus.Name);
			}
		}

	}

	bool AudioEngine::Init(bool forceNullDevice)
	{
		if (s_Audio.Initialized)
			return true;

		ma_engine_config config = ma_engine_config_init();
		if (!forceNullDevice && ma_engine_init(&config, &s_Audio.Engine) == MA_SUCCESS)
		{
			s_Audio.Initialized = true;
			s_Audio.NullDevice = false;
			InitBuses();
			BW_CORE_INFO("Audio engine ready ({} Hz)", ma_engine_get_sample_rate(&s_Audio.Engine));
			return true;
		}

		// No usable device: run the full mixer without output so behaviour stays identical.
		config = ma_engine_config_init();
		config.noDevice = MA_TRUE;
		config.channels = 2;
		config.sampleRate = 48000;
		if (ma_engine_init(&config, &s_Audio.Engine) != MA_SUCCESS)
		{
			BW_CORE_ERROR("Failed to initialize the audio engine");
			return false;
		}
		s_Audio.Initialized = true;
		s_Audio.NullDevice = true;
		InitBuses();
		BW_CORE_INFO("Audio engine ready (no output device, sound is silent)");
		return true;
	}

	void AudioEngine::Shutdown()
	{
		if (!s_Audio.Initialized)
			return;
		ReleaseOneShots(true);
		for (Bus& bus : s_Audio.Buses)
		{
			if (bus.GroupReady)
				ma_sound_group_uninit(&bus.Group);
			bus.GroupReady = false;
		}
		ma_engine_uninit(&s_Audio.Engine);
		s_Audio.Initialized = false;
	}

	bool AudioEngine::IsInitialized()
	{
		return s_Audio.Initialized;
	}

	bool AudioEngine::IsNullDevice()
	{
		return s_Audio.NullDevice;
	}

	ma_engine* AudioEngine::GetEngine()
	{
		return s_Audio.Initialized ? &s_Audio.Engine : nullptr;
	}

	void AudioEngine::SetMasterVolume(float volume)
	{
		SetBusVolume("Master", volume);
	}

	float AudioEngine::GetMasterVolume()
	{
		return s_Audio.Initialized ? GetBusVolume("Master") : 0.0f;
	}

	const std::vector<std::string>& AudioEngine::GetBusNames()
	{
		static const std::vector<std::string> s_Names = { "Master", "Music", "Effects" };
		return s_Names;
	}

	bool AudioEngine::IsBus(const std::string& bus)
	{
		return FindBus(bus) != nullptr;
	}

	bool AudioEngine::SetBusVolume(const std::string& name, float volume)
	{
		Bus* bus = FindBus(name);
		if (!bus)
			return false;
		bus->Volume = std::max(volume, 0.0f);
		ApplyBusVolume(*bus);
		return true;
	}

	float AudioEngine::GetBusVolume(const std::string& name)
	{
		const Bus* bus = FindBus(name);
		return bus ? bus->Volume : 0.0f;
	}

	bool AudioEngine::SetBusMuted(const std::string& name, bool muted)
	{
		Bus* bus = FindBus(name);
		if (!bus)
			return false;
		bus->Muted = muted;
		ApplyBusVolume(*bus);
		return true;
	}

	bool AudioEngine::IsBusMuted(const std::string& name)
	{
		const Bus* bus = FindBus(name);
		return bus && bus->Muted;
	}

	ma_sound_group* AudioEngine::GetBusGroup(const std::string& name)
	{
		Bus* bus = FindBus(name);
		return s_Audio.Initialized && bus && bus->GroupReady ? &bus->Group : nullptr;
	}

	bool AudioEngine::PlayOneShot(const std::string& path, float volume, const std::string& bus)
	{
		if (!s_Audio.Initialized)
			return false;
		if (!IsBus(bus))
		{
			BW_CORE_ERROR("Audio: unknown bus '{}'", bus);
			return false;
		}

		const std::filesystem::path absolutePath = ResolveAudioPath(path);
		auto* sound = new ma_sound();
		if (ma_sound_init_from_file(&s_Audio.Engine, absolutePath.string().c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
			GetBusGroup(bus), nullptr, sound) != MA_SUCCESS)
		{
			delete sound;
			BW_CORE_ERROR("Audio: could not load '{}'", path);
			return false;
		}

		ma_sound_set_volume(sound, volume);
		ma_sound_start(sound);

		ReleaseOneShots(false);
		s_Audio.OneShots.push_back(sound);
		return true;
	}

	void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
	{
		if (!s_Audio.Initialized)
			return;
		ma_engine_listener_set_position(&s_Audio.Engine, 0, position.x, position.y, position.z);
		ma_engine_listener_set_direction(&s_Audio.Engine, 0, forward.x, forward.y, forward.z);
		ma_engine_listener_set_world_up(&s_Audio.Engine, 0, up.x, up.y, up.z);
	}

	SceneAudio::SceneAudio(Scene& scene)
		: m_Scene(scene)
	{
	}

	SceneAudio::~SceneAudio()
	{
		for (auto& [uuid, sound] : m_Sounds)
		{
			ma_sound_stop(sound);
			ma_sound_uninit(sound);
			delete sound;
		}
		m_Sounds.clear();
	}

	ma_sound* SceneAudio::GetOrCreateSound(Entity entity)
	{
		if (!AudioEngine::IsInitialized() || !entity.HasComponent<AudioSourceComponent>())
			return nullptr;

		const AudioSourceComponent& source = entity.GetComponent<AudioSourceComponent>();
		const UUID uuid = entity.GetUUID();

		// A sound is made for one clip on one bus; changing either rebuilds it.
		const std::string bus = AudioEngine::IsBus(source.Bus) ? source.Bus : std::string("Effects");
		const std::string key = source.ClipPath + "|" + bus;
		auto it = m_Sounds.find(uuid);
		if (it != m_Sounds.end())
		{
			if (m_SoundKeys[uuid] == key)
				return it->second;
			ma_sound_stop(it->second);
			ma_sound_uninit(it->second);
			delete it->second;
			m_Sounds.erase(it);
		}

		if (source.ClipPath.empty())
			return nullptr;

		const std::filesystem::path path = ResolveAudioPath(source.ClipPath);
		std::error_code error;
		const bool stream = std::filesystem::file_size(path, error) > 4 * 1024 * 1024;

		auto* sound = new ma_sound();
		ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
		if (!source.Spatial)
			flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

		if (ma_sound_init_from_file(AudioEngine::GetEngine(), path.string().c_str(), flags, AudioEngine::GetBusGroup(bus), nullptr, sound) != MA_SUCCESS)
		{
			delete sound;
			BW_CORE_ERROR("Audio: could not load '{}' for '{}'", source.ClipPath, entity.GetName());
			m_SoundKeys[uuid] = key;
			return nullptr;
		}

		m_Sounds[uuid] = sound;
		m_SoundKeys[uuid] = key;
		ApplySettings(entity);
		return sound;
	}

	void SceneAudio::ApplySettings(Entity entity)
	{
		auto it = m_Sounds.find(entity.GetUUID());
		if (it == m_Sounds.end() || !entity.HasComponent<AudioSourceComponent>())
			return;

		const AudioSourceComponent& source = entity.GetComponent<AudioSourceComponent>();
		ma_sound* sound = it->second;
		ma_sound_set_volume(sound, std::max(source.Volume, 0.0f));
		ma_sound_set_pitch(sound, std::max(source.Pitch, 0.01f));
		ma_sound_set_looping(sound, source.Loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_spatialization_enabled(sound, source.Spatial ? MA_TRUE : MA_FALSE);
		ma_sound_set_min_distance(sound, std::max(source.MinDistance, 0.01f));
		ma_sound_set_max_distance(sound, std::max(source.MaxDistance, source.MinDistance));
		ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
	}

	void SceneAudio::OnRuntimeStart()
	{
		auto view = m_Scene.GetAllEntitiesWith<AudioSourceComponent>();
		for (entt::entity handle : view)
		{
			Entity entity(handle, &m_Scene);
			if (entity.GetComponent<AudioSourceComponent>().PlayOnStart)
				Play(entity);
		}
	}

	void SceneAudio::OnEntitiesLoaded(const std::vector<Entity>& entities)
	{
		for (Entity entity : entities)
		{
			const auto* source = entity.TryGetComponent<AudioSourceComponent>();
			if (source && source->PlayOnStart)
				Play(entity);
		}
	}

	void SceneAudio::OnUpdate()
	{
		if (!AudioEngine::IsInitialized())
			return;

		// Listener: the first active AudioListener, otherwise the primary camera.
		Entity listener;
		auto listeners = m_Scene.GetAllEntitiesWith<AudioListenerComponent>();
		for (entt::entity handle : listeners)
		{
			if (listeners.get<AudioListenerComponent>(handle).Active)
			{
				listener = Entity(handle, &m_Scene);
				break;
			}
		}
		if (!listener)
			listener = m_Scene.GetPrimaryCameraEntity();
		if (listener)
		{
			const glm::mat4 world = m_Scene.GetWorldTransform(listener);
			AudioEngine::SetListener(glm::vec3(world[3]), glm::normalize(-glm::vec3(world[2])), glm::normalize(glm::vec3(world[1])));
		}

		for (auto& [uuid, sound] : m_Sounds)
		{
			Entity entity = m_Scene.TryGetEntityByUUID(uuid);
			if (!entity)
				continue;
			const glm::vec3 position = m_Scene.GetWorldPosition(entity);
			ma_sound_set_position(sound, position.x, position.y, position.z);
		}
	}

	void SceneAudio::OnEntityDestroyed(Entity entity)
	{
		auto it = m_Sounds.find(entity.GetUUID());
		if (it == m_Sounds.end())
			return;
		ma_sound_stop(it->second);
		ma_sound_uninit(it->second);
		delete it->second;
		m_Sounds.erase(it);
		m_SoundKeys.erase(entity.GetUUID());
	}

	bool SceneAudio::Play(Entity entity)
	{
		ma_sound* sound = GetOrCreateSound(entity);
		if (!sound)
			return false;
		ApplySettings(entity);
		const glm::vec3 position = m_Scene.GetWorldPosition(entity);
		ma_sound_set_position(sound, position.x, position.y, position.z);
		ma_sound_seek_to_pcm_frame(sound, 0);
		return ma_sound_start(sound) == MA_SUCCESS;
	}

	void SceneAudio::Stop(Entity entity)
	{
		auto it = m_Sounds.find(entity.GetUUID());
		if (it != m_Sounds.end())
		{
			ma_sound_stop(it->second);
			ma_sound_seek_to_pcm_frame(it->second, 0);
		}
	}

	void SceneAudio::Pause(Entity entity)
	{
		auto it = m_Sounds.find(entity.GetUUID());
		if (it != m_Sounds.end())
			ma_sound_stop(it->second);
	}

	bool SceneAudio::IsPlaying(Entity entity) const
	{
		auto it = m_Sounds.find(entity.GetUUID());
		return it != m_Sounds.end() && ma_sound_is_playing(it->second);
	}

}
