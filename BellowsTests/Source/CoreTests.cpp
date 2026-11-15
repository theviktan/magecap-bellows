#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Buffer.h"
#include "Bellows/Core/Events.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Core/UUID.h"

#include <unordered_set>

using namespace Bellows;

TEST_CASE("UUIDs are unique and never zero")
{
	std::unordered_set<uint64_t> seen;
	for (int i = 0; i < 10000; i++)
	{
		UUID uuid;
		CHECK(uuid.IsValid());
		CHECK(seen.insert(uuid).second);
	}
	CHECK_FALSE(UUID(0).IsValid());
	CHECK(static_cast<uint64_t>(UUID(42)) == 42);
}

TEST_CASE("Buffer owns, copies and moves its memory")
{
	const char text[] = "bellows";
	Buffer original = Buffer::Copy(text, sizeof(text));
	REQUIRE(original);
	CHECK(original.Size() == sizeof(text));
	CHECK(std::memcmp(original.Data(), text, sizeof(text)) == 0);

	Buffer moved = std::move(original);
	CHECK(moved.Size() == sizeof(text));
	CHECK_FALSE(original);
	CHECK(original.Size() == 0);

	Buffer empty(0);
	CHECK_FALSE(empty);
}

TEST_CASE("FileSystem writes atomically and reads back")
{
	const std::filesystem::path directory = BellowsTests::GetTestOutputDirectory() / "FileSystem" / "Nested";
	const std::filesystem::path file = directory / "data.txt";

	REQUIRE(FileSystem::WriteTextFile(file, "first"));
	REQUIRE(FileSystem::WriteTextFile(file, "second version"));
	CHECK(FileSystem::ReadTextFile(file).value_or("") == "second version");
	CHECK_FALSE(FileSystem::Exists(directory / "data.txt.tmp"));

	const uint8_t bytes[] = { 0, 1, 2, 250, 255 };
	REQUIRE(FileSystem::WriteBinaryFile(directory / "data.bin", bytes, sizeof(bytes)));
	Buffer read = FileSystem::ReadBinaryFile(directory / "data.bin");
	REQUIRE(read.Size() == sizeof(bytes));
	CHECK(std::memcmp(read.Data(), bytes, sizeof(bytes)) == 0);

	CHECK_FALSE(FileSystem::ReadTextFile(directory / "missing.txt").has_value());
	CHECK_FALSE(FileSystem::ReadBinaryFile(directory / "missing.bin"));
}

TEST_CASE("FileSystem makes paths relative to a base folder")
{
	const std::filesystem::path base = BellowsTests::GetTestOutputDirectory() / "Project";
	CHECK(FileSystem::MakeRelativeGenericPath(base / "Models" / "Ship.glb", base) == "Models/Ship.glb");
	CHECK(FileSystem::MakeRelativeGenericPath(base.parent_path() / "Other.txt", base).empty());
}

TEST_CASE("Input tracks down, pressed and released per frame")
{
	Input::ResetState();
	Input::BeginFrame();
	Input::SetEnabled(true);

	Input::InjectKey(KeyCode::Space, true);
	CHECK(Input::IsKeyDown(KeyCode::Space));
	CHECK(Input::IsKeyPressed(KeyCode::Space));
	CHECK_FALSE(Input::IsKeyReleased(KeyCode::Space));

	Input::BeginFrame();
	CHECK(Input::IsKeyDown(KeyCode::Space));
	CHECK_FALSE(Input::IsKeyPressed(KeyCode::Space));

	KeyReleasedEvent release(KeyCode::Space);
	Input::OnEvent(release);
	CHECK_FALSE(Input::IsKeyDown(KeyCode::Space));
	CHECK(Input::IsKeyReleased(KeyCode::Space));

	Input::BeginFrame();
	CHECK_FALSE(Input::IsKeyReleased(KeyCode::Space));

	// Disabled input ignores presses but never leaves keys stuck.
	Input::InjectKey(KeyCode::A, true);
	Input::SetEnabled(false);
	CHECK_FALSE(Input::IsKeyDown(KeyCode::A));
	KeyPressedEvent press(KeyCode::B, false);
	Input::OnEvent(press);
	CHECK_FALSE(Input::IsKeyDown(KeyCode::B));
	Input::SetEnabled(true);

	Input::SetViewportRect({ 100.0f, 50.0f }, { 640.0f, 360.0f });
	MouseMovedEvent move(150.0f, 80.0f);
	Input::OnEvent(move);
	CHECK(Input::GetMousePosition() == glm::vec2(50.0f, 30.0f));
	Input::InjectMousePosition({ 10.0f, 20.0f });
	CHECK(Input::GetMousePosition() == glm::vec2(10.0f, 20.0f));

	Input::InjectMouseButton(MouseButton::Left, true);
	CHECK(Input::IsMouseButtonPressed(MouseButton::Left));
	Input::ResetState();
	CHECK_FALSE(Input::IsMouseButtonDown(MouseButton::Left));
	Input::SetViewportRect({ 0.0f, 0.0f }, { 0.0f, 0.0f });
}

TEST_CASE("EventDispatcher routes by type and marks events handled")
{
	WindowResizeEvent resize(800, 600);
	EventDispatcher dispatcher(resize);
	bool keyCalled = false;
	dispatcher.Dispatch<KeyPressedEvent>([&](KeyPressedEvent&) { keyCalled = true; return true; });
	CHECK_FALSE(keyCalled);
	CHECK_FALSE(resize.Handled);

	uint32_t width = 0;
	dispatcher.Dispatch<WindowResizeEvent>([&](WindowResizeEvent& e) { width = e.GetWidth(); return true; });
	CHECK(width == 800);
	CHECK(resize.Handled);
}

TEST_CASE("Log keeps a history and counts errors")
{
	const uint64_t before = Log::GetLatestSequence();
	const uint32_t errorsBefore = Log::GetErrorCount();
	BW_CORE_INFO("history test message");
	BW_ERROR("history test error");

	const std::vector<LogMessage> messages = Log::GetMessagesSince(before);
	REQUIRE(messages.size() >= 2);
	CHECK(messages[messages.size() - 2].Text == "history test message");
	CHECK(messages.back().Level == LogLevel::Error);
	CHECK(messages.back().Source == LogSource::Client);
	CHECK(Log::GetErrorCount() == errorsBefore + 1);
}
