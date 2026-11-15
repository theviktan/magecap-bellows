#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"

#include <glm/gtc/constants.hpp>

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
