#include <doctest/doctest.h>

#include "AgentServer.h"
#include "TestUtilities.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"

#include <chrono>
#include <future>
#include <set>
#include <string>
#include <thread>

#if defined(_WIN32)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	using TestSocket = SOCKET;
	constexpr TestSocket c_NoSocket = INVALID_SOCKET;
	inline void CloseTestSocket(TestSocket s) { closesocket(s); }
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <sys/stat.h>
	#include <sys/time.h>
	#include <unistd.h>
	using TestSocket = int;
	constexpr TestSocket c_NoSocket = -1;
	inline void CloseTestSocket(TestSocket s) { close(s); }
#endif

using namespace Bellows;

namespace {

	// The session file goes to a test folder, never the user's real config.
	class ScopedAgentConfig
	{
	public:
		ScopedAgentConfig()
			: m_Previous(Platform::GetEnvVar("BELLOWS_CONFIG_DIR"))
		{
			const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "AgentServer";
			std::error_code error;
			std::filesystem::remove_all(folder, error);
			std::filesystem::create_directories(folder);
			Platform::SetEnvVar("BELLOWS_CONFIG_DIR", folder.string());
		}
		~ScopedAgentConfig() { Platform::SetEnvVar("BELLOWS_CONFIG_DIR", m_Previous); }
	private:
		std::string m_Previous;
	};

	// A client connection to the server, with a receive timeout so a test can't hang.
	class Client
	{
	public:
		explicit Client(uint16_t port)
		{
			m_Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			REQUIRE(m_Socket != c_NoSocket);
			sockaddr_in address {};
			address.sin_family = AF_INET;
			address.sin_port = htons(port);
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			REQUIRE(connect(m_Socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
#if defined(_WIN32)
			const DWORD timeout = 5000;
			setsockopt(m_Socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
			const timeval timeout { 5, 0 };
			setsockopt(m_Socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
		}
		~Client() { CloseTestSocket(m_Socket); }
		Client(const Client&) = delete;
		Client& operator=(const Client&) = delete;

		void Send(const std::string& text)
		{
			REQUIRE(send(m_Socket, text.data(), static_cast<int>(text.size()), 0) == static_cast<int>(text.size()));
		}

		// One line of answer, or "<closed>" when the server closed the connection, "<timeout>" when nothing came.
		std::string ReadLine()
		{
			std::string line;
			char c = 0;
			while (true)
			{
				const auto received = recv(m_Socket, &c, 1, 0);
				if (received == 0)
					return "<closed>";
				if (received < 0)
					return line.empty() ? "<timeout>" : line;
				if (c == '\n')
					return line;
				line.push_back(c);
			}
		}
	private:
		TestSocket m_Socket = c_NoSocket;
	};

	// Runs 'work' on another thread while the test's main thread serves requests, as the editor does.
	template<typename Function>
	auto ServeWhile(AgentServer& server, Function work)
	{
		auto result = std::async(std::launch::async, work);
		while (result.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready)
			server.ProcessRequests();
		return result.get();
	}

	std::string Request(const std::string& token, const std::string& method, const std::string& params = "{}")
	{
		return R"({"id": 7, "token": ")" + token + R"(", "method": ")" + method + R"(", "params": )" + params + "}\n";
	}

}

TEST_CASE("Agent server: session tokens, HTTP lines and token comparison")
{
	std::set<std::string> tokens;
	for (int i = 0; i < 8; i++)
	{
		const std::string token = AgentServer::GenerateSessionToken();
		CHECK(token.size() == 64);
		CHECK(token.find_first_not_of("0123456789abcdef") == std::string::npos);
		tokens.insert(token);
	}
	CHECK(tokens.size() == 8);

	CHECK(AgentServer::LooksLikeHttp("POST / HTTP/1.1"));
	CHECK(AgentServer::LooksLikeHttp("GET /agent HTTP/1.0\r"));
	CHECK(AgentServer::LooksLikeHttp("OPTIONS * HTTP/1.1"));
	CHECK(AgentServer::LooksLikeHttp("  PUT /x HTTP/2"));
	CHECK(AgentServer::LooksLikeHttp("BREW /pot HTTP/1.1"));		// Any method word, by its HTTP version
	CHECK_FALSE(AgentServer::LooksLikeHttp(R"({"id": 1, "method": "ping"})"));
	CHECK_FALSE(AgentServer::LooksLikeHttp(""));
	CHECK_FALSE(AgentServer::LooksLikeHttp("GETTING"));

	CHECK(AgentServer::TokensMatch("abc123", "abc123"));
	CHECK_FALSE(AgentServer::TokensMatch("abc124", "abc123"));
	CHECK_FALSE(AgentServer::TokensMatch("abc12", "abc123"));
	CHECK_FALSE(AgentServer::TokensMatch("", ""));					// No token set: nothing matches
}

TEST_CASE("Agent server: answers its own clients, and never a web page")
{
	ScopedAgentConfig config;
	std::atomic<int> calls = 0;
	AgentServer server;
	REQUIRE(server.Start(47940, [&](const std::string& method, const Json&) -> Json
	{
		calls++;
		if (method == "fail")
			throw std::runtime_error("It failed");
		return method;
	}));
	const std::string token = server.GetToken();
	REQUIRE(token.size() == 64);

	SUBCASE("A request with the token is run on the main thread and answered")
	{
		const std::string answer = ServeWhile(server, [&]
		{
			Client client(server.GetPort());
			client.Send(Request(token, "ping"));
			return client.ReadLine();
		});
		const Json response = Json::parse(answer, nullptr, false);
		CHECK(response.value("ok", false));
		CHECK(response.value("result", std::string()) == "ping");
		CHECK(response.value("id", 0) == 7);
		CHECK(calls == 1);
	}
	SUBCASE("A wrong, short or missing token is refused without running anything")
	{
		const std::string wrong = token.substr(0, 63) + (token.back() == 'a' ? "b" : "a");
		for (const std::string& request : { Request(wrong, "exec"), Request(token.substr(0, 32), "exec"),
			std::string(R"({"id": 7, "method": "exec"})") + "\n", std::string(R"({"id": 7, "token": 5, "method": "exec"})") + "\n" })
		{
			const std::string answer = ServeWhile(server, [&]
			{
				Client client(server.GetPort());
				client.Send(request);
				return client.ReadLine();
			});
			CHECK(answer.find("Invalid or missing session token") != std::string::npos);
		}
		CHECK(calls == 0);
	}
	SUBCASE("A browser's POST, even with the right token in its body, is dropped unanswered")
	{
		const std::string answer = ServeWhile(server, [&]
		{
			Client client(server.GetPort());
			client.Send("POST / HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(server.GetPort()) +
				"\r\nContent-Type: text/plain\r\nOrigin: https://example.com\r\n\r\n\n" + Request(token, "exec"));
			return client.ReadLine();
		});
		CHECK(answer == "<closed>");
		CHECK(calls == 0);
	}
	SUBCASE("An idle client doesn't hold up another one")
	{
		Client idle(server.GetPort());
		idle.Send(R"({"id": 1, "token": ")");		// Half a line, never finished
		const std::string answer = ServeWhile(server, [&]
		{
			Client client(server.GetPort());
			client.Send(Request(token, "ping"));
			return client.ReadLine();
		});
		CHECK(Json::parse(answer, nullptr, false).value("ok", false));
	}
	SUBCASE("Errors come back as answers: a failing handler and a bad wait_frames")
	{
		const auto ask = [&](const std::string& request)
		{
			return Json::parse(ServeWhile(server, [&]
			{
				Client client(server.GetPort());
				client.Send(request);
				return client.ReadLine();
			}), nullptr, false);
		};
		const Json failed = ask(Request(token, "fail"));
		CHECK_FALSE(failed.value("ok", true));
		CHECK(failed.value("error", std::string()) == "It failed");
		const Json badWait = ask(Request(token, "wait_frames", R"({"frames": "lots"})"));
		CHECK_FALSE(badWait.value("ok", true));
		CHECK(badWait.value("error", std::string()).find("whole number") != std::string::npos);
		const Json waited = ask(Request(token, "wait_frames", R"({"frames": 2})"));
		CHECK(waited.value("ok", false));
	}
	SUBCASE("The session file has the port and token, and only its owner can read it")
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(AgentServer::GetSessionFilePath());
		REQUIRE(text);
		const Json session = Json::parse(*text, nullptr, false);
		CHECK(session.value("token", std::string()) == token);
		CHECK(session.value("port", 0) == server.GetPort());
#if !defined(_WIN32)
		struct stat info {};
		REQUIRE(stat(AgentServer::GetSessionFilePath().c_str(), &info) == 0);
		CHECK((info.st_mode & 0777) == 0600);
#endif
	}

	server.Stop();
	CHECK_FALSE(std::filesystem::exists(AgentServer::GetSessionFilePath()));
}
