#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <glm/gtc/constants.hpp>
#include <miniaudio.h>

using namespace Bellows;

namespace {

	// Writes a 16-bit mono PCM WAV file with a sine tone.
	void WriteSineWav(const std::filesystem::path& path, float seconds, float frequency)
	{
		const uint32_t sampleRate = 44100;
		const uint32_t sampleCount = static_cast<uint32_t>(seconds * sampleRate);
		std::vector<int16_t> samples(sampleCount);
		for (uint32_t i = 0; i < sampleCount; i++)
			samples[i] = static_cast<int16_t>(std::sin(glm::two_pi<float>() * frequency * static_cast<float>(i) / sampleRate) * 12000.0f);

		std::vector<uint8_t> file;
		auto write32 = [&](uint32_t v) { for (int b = 0; b < 4; b++) file.push_back(static_cast<uint8_t>(v >> (8 * b))); };
		auto write16 = [&](uint16_t v) { for (int b = 0; b < 2; b++) file.push_back(static_cast<uint8_t>(v >> (8 * b))); };
		auto writeTag = [&](const char* tag) { file.insert(file.end(), tag, tag + 4); };

		const uint32_t dataSize = sampleCount * 2;
		writeTag("RIFF"); write32(36 + dataSize); writeTag("WAVE");
		writeTag("fmt "); write32(16); write16(1); write16(1); write32(sampleRate); write32(sampleRate * 2); write16(2); write16(16);
		writeTag("data"); write32(dataSize);
		const auto* bytes = reinterpret_cast<const uint8_t*>(samples.data());
		file.insert(file.end(), bytes, bytes + dataSize);
		REQUIRE(FileSystem::WriteBinaryFile(path, file.data(), file.size()));
	}

}

TEST_CASE("Audio sources play through the (null) audio engine")
{
	REQUIRE(AudioEngine::IsInitialized());
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "AudioProject";
	WriteSineWav(assets / "Audio" / "Tone.wav", 0.5f, 440.0f);
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> scene = CreateRef<Scene>();
	Entity speaker = scene->CreateEntity("Speaker");
	auto& source = speaker.AddComponent<AudioSourceComponent>();
	source.ClipPath = "Audio/Tone.wav";
	source.PlayOnStart = true;
	source.Loop = true;
	Entity listener = scene->CreateEntity("Listener");
	listener.AddComponent<AudioListenerComponent>();

	scene->OnRuntimeStart();
	REQUIRE(scene->GetAudio());
	CHECK(scene->GetAudio()->IsPlaying(speaker));
	CHECK(scene->GetAudio()->GetSoundCount() == 1);
	scene->OnUpdateRuntime(1.0f / 60.0f);

	scene->GetAudio()->Stop(speaker);
	CHECK_FALSE(scene->GetAudio()->IsPlaying(speaker));
	CHECK(scene->GetAudio()->Play(speaker));
	CHECK(AudioEngine::PlayOneShot("Audio/Tone.wav", 0.5f));
	CHECK_FALSE(AudioEngine::PlayOneShot("Audio/Missing.wav"));

	scene->DestroyEntity(speaker);
	scene->OnUpdateRuntime(1.0f / 60.0f);
	CHECK(scene->GetAudio()->GetSoundCount() == 0);
	scene->OnRuntimeStop();

	AudioEngine::SetMasterVolume(0.25f);
	CHECK(AudioEngine::GetMasterVolume() == doctest::Approx(0.25f));
	AudioEngine::SetMasterVolume(1.0f);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Audio clips decode from every supported format")
{
	// .wav is written here; .ogg is a checked-in 0.3 second tone (Ogg Vorbis needs an encoder).
	const std::filesystem::path wav = BellowsTests::GetTestOutputDirectory() / "Formats" / "Tone.wav";
	WriteSineWav(wav, 0.3f, 440.0f);
	const std::filesystem::path ogg = BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "Tone.ogg";
	REQUIRE(FileSystem::Exists(ogg));

	for (const std::filesystem::path& path : { wav, ogg })
	{
		CAPTURE(path.filename().string());
		ma_decoder decoder;
		REQUIRE(ma_decoder_init_file(path.string().c_str(), nullptr, &decoder) == MA_SUCCESS);
		ma_uint64 frames = 0;
		CHECK(ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS);
		CHECK(frames > 0);
		float samples[256] = {};
		ma_uint64 read = 0;
		CHECK(ma_decoder_read_pcm_frames(&decoder, samples, 64, &read) == MA_SUCCESS);
		CHECK(read == 64);
		ma_decoder_uninit(&decoder);
	}

	// And through the engine, the way an Audio Source loads it.
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "OggProject";
	REQUIRE(FileSystem::CreateDirectories(assets / "Audio"));
	std::filesystem::copy_file(ogg, assets / "Audio" / "Tone.ogg", std::filesystem::copy_options::overwrite_existing);
	AssetManager::SetAssetDirectory(assets);
	CHECK(AudioEngine::PlayOneShot("Audio/Tone.ogg", 0.5f));
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Audio buses set volumes for groups of sounds")
{
	REQUIRE(AudioEngine::IsInitialized());
	CHECK(AudioEngine::GetBusNames() == std::vector<std::string> { "Master", "Music", "Effects" });
	CHECK(AudioEngine::IsBus("Music"));
	CHECK_FALSE(AudioEngine::IsBus("music"));
	CHECK_FALSE(AudioEngine::SetBusVolume("Voices", 0.5f));
	CHECK_FALSE(AudioEngine::PlayOneShot("Audio/Tone.wav", 1.0f, "Voices"));

	// The engine's own volume is the Master bus; the others are miniaudio groups.
	CHECK(AudioEngine::GetBusGroup("Master") == nullptr);
	REQUIRE(AudioEngine::GetBusGroup("Music") != nullptr);
	CHECK(AudioEngine::SetBusVolume("Music", 0.4f));
	CHECK(AudioEngine::GetBusVolume("Music") == doctest::Approx(0.4f));
	CHECK(ma_sound_group_get_volume(AudioEngine::GetBusGroup("Music")) == doctest::Approx(0.4f));

	// Muting silences the bus but keeps its volume for unmuting.
	CHECK(AudioEngine::SetBusMuted("Music", true));
	CHECK(AudioEngine::IsBusMuted("Music"));
	CHECK(ma_sound_group_get_volume(AudioEngine::GetBusGroup("Music")) == doctest::Approx(0.0f));
	CHECK(AudioEngine::GetBusVolume("Music") == doctest::Approx(0.4f));
	CHECK(AudioEngine::SetBusMuted("Music", false));
	CHECK(ma_sound_group_get_volume(AudioEngine::GetBusGroup("Music")) == doctest::Approx(0.4f));

	CHECK(AudioEngine::SetBusVolume("Master", 0.5f));
	CHECK(AudioEngine::GetMasterVolume() == doctest::Approx(0.5f));
	AudioEngine::SetMasterVolume(1.0f);
	AudioEngine::SetBusVolume("Music", 1.0f);

	// A source plays on its bus; changing the bus rebuilds its sound there.
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "AudioBusProject";
	WriteSineWav(assets / "Audio" / "Tone.wav", 0.2f, 330.0f);
	AssetManager::SetAssetDirectory(assets);
	Ref<Scene> scene = CreateRef<Scene>();
	Entity speaker = scene->CreateEntity("Speaker");
	auto& source = speaker.AddComponent<AudioSourceComponent>();
	CHECK(source.Bus == "Effects");
	source.ClipPath = "Audio/Tone.wav";
	source.Bus = "Music";
	scene->OnRuntimeStart();
	CHECK(scene->GetAudio()->Play(speaker));
	source.Bus = "Effects";
	CHECK(scene->GetAudio()->Play(speaker));
	CHECK(scene->GetAudio()->GetSoundCount() == 1);
	source.Bus = "Nonsense";		// Unknown buses fall back to Effects instead of failing
	CHECK(scene->GetAudio()->Play(speaker));
	CHECK(AudioEngine::PlayOneShot("Audio/Tone.wav", 0.5f, "Music"));
	scene->OnRuntimeStop();
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Kept music keeps playing through load_scene, and the new scene's sounds start")
{
	REQUIRE(AudioEngine::IsInitialized());
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "AudioSceneChange";
	WriteSineWav(assets / "Audio" / "Tone.wav", 0.5f, 440.0f);
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> scene = CreateRef<Scene>();
	Entity music = scene->CreateEntity("Music");
	music.GetComponent<TagComponent>().KeepAcrossScenes = true;
	auto& musicSource = music.AddComponent<AudioSourceComponent>();
	musicSource.ClipPath = "Audio/Tone.wav";
	musicSource.PlayOnStart = true;
	musicSource.Loop = true;
	Entity hum = scene->CreateEntity("Old level's hum");
	auto& humSource = hum.AddComponent<AudioSourceComponent>();
	humSource.ClipPath = "Audio/Tone.wav";
	humSource.PlayOnStart = true;
	humSource.Loop = true;

	Scene level("Level 2");
	Entity bell = level.CreateEntity("Bell");
	auto& bellSource = bell.AddComponent<AudioSourceComponent>();
	bellSource.ClipPath = "Audio/Tone.wav";
	bellSource.PlayOnStart = true;
	Entity quiet = level.CreateEntity("Quiet");
	quiet.AddComponent<AudioSourceComponent>().ClipPath = "Audio/Tone.wav";

	const UUID humID = hum.GetUUID();
	scene->OnRuntimeStart();
	scene->OnUpdateRuntime(1.0f / 60.0f);
	REQUIRE(scene->GetAudio()->GetSoundCount() == 2);

	REQUIRE(scene->ChangeScene(SceneSerializer::SerializeToJson(level), "Scenes/Level2.bscene"));
	CHECK(scene->GetAudio()->IsPlaying(music));
	CHECK(scene->GetAudio()->IsPlaying(scene->FindEntityByName("Bell")));
	CHECK_FALSE(scene->GetAudio()->IsPlaying(scene->FindEntityByName("Quiet")));
	CHECK_FALSE(scene->TryGetEntityByUUID(humID));
	CHECK(scene->GetAudio()->GetSoundCount() == 2);

	scene->OnRuntimeStop();
	AssetManager::SetAssetDirectory({});
}
