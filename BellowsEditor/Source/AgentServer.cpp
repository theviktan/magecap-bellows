#include "AgentServer.h"

#include "EditorSettings.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	#include <bcrypt.h>
	using SocketHandle = SOCKET;
	constexpr SocketHandle c_InvalidSocket = INVALID_SOCKET;
	inline void CloseSocket(SocketHandle s) { closesocket(s); }
	#define getpid _getpid
	#include <process.h>
#else
	#include <arpa/inet.h>
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <sys/random.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <sys/stat.h>
	#include <unistd.h>
	using SocketHandle = int;
	constexpr SocketHandle c_InvalidSocket = -1;
	inline void CloseSocket(SocketHandle s) { close(s); }
#endif

namespace Bellows {

	namespace {

		constexpr auto c_LineTimeout = std::chrono::seconds(30);	// A request line has this long to arrive whole

		bool ReadSecureRandom(uint8_t* data, size_t size)
		{
#if defined(_WIN32)
			return BCryptGenRandom(nullptr, data, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
			size_t filled = 0;
			while (filled < size)
			{
				const ssize_t result = getrandom(data + filled, size - filled, 0);
				if (result < 0)
				{
					if (errno == EINTR)
						continue;
					return false;
				}
				filled += static_cast<size_t>(result);
			}
			return true;
#endif
		}

		bool SendAll(SocketHandle socket, const std::string& data)
		{
			size_t sent = 0;
			while (sent < data.size())
			{
				const auto result = send(socket, data.data() + sent, static_cast<int>(data.size() - sent), 0);
				if (result <= 0)
					return false;
				sent += static_cast<size_t>(result);
			}
			return true;
		}

		std::string ErrorResponse(const Json& id, const std::string& message)
		{
			Json response;
			response["id"] = id;
			response["ok"] = false;
			response["error"] = message;
			return response.dump();
		}

	}

	AgentServer::~AgentServer()
	{
		Stop();
	}

	std::filesystem::path AgentServer::GetSessionFilePath()
	{
		return EditorSettings::GetConfigDirectory() / "AgentSession.json";
	}

	bool AgentServer::Start(uint16_t port, Handler handler)
	{
		if (m_Running)
			return true;

#if defined(_WIN32)
		WSADATA data;
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
			return false;
#endif

		for (uint16_t candidate = port; candidate < port + 10; candidate++)
		{
			SocketHandle listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (listenSocket == c_InvalidSocket)
				continue;

			sockaddr_in address {};
			address.sin_family = AF_INET;
			address.sin_port = htons(candidate);
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	// Never reachable from other machines

			if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listenSocket, 4) == 0)
			{
				m_ListenSocket = static_cast<intptr_t>(listenSocket);
				m_Port = candidate;
				break;
			}
			CloseSocket(listenSocket);
		}

		if (m_ListenSocket == -1)
		{
			BW_CORE_WARN("Agent server: no free port in {}-{}", port, port + 9);
			return false;
		}

		m_Token = GenerateSessionToken();
		if (m_Token.empty() || !WriteSessionFile())
		{
			BW_CORE_ERROR("Agent server: couldn't make a session token or write {}; it stays off", GetSessionFilePath().string());
			CloseSocket(static_cast<SocketHandle>(m_ListenSocket));
			m_ListenSocket = -1;
			m_Token.clear();
			return false;
		}
		m_Handler = std::move(handler);
		m_Running = true;
		m_Thread = std::thread([this] { ServerThread(); });

		BW_CORE_INFO("Agent server listening on 127.0.0.1:{}", m_Port);
		return true;
	}

	bool AgentServer::WriteSessionFile() const
	{
		Json session;
		session["port"] = m_Port;
		session["token"] = m_Token;
		session["pid"] = static_cast<int64_t>(getpid());
		const std::string text = session.dump(1, '\t');
		const std::filesystem::path path = GetSessionFilePath();
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
#if defined(_WIN32)
		// The config folder is under the user's profile, which only they (and administrators) can read.
		return FileSystem::WriteTextFile(path, text);
#else
		// Only the user may read the token: made with mode 0600, and set again in case the file was there.
		const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
		if (file < 0)
			return false;
		bool written = fchmod(file, S_IRUSR | S_IWUSR) == 0;
		size_t offset = 0;
		while (written && offset < text.size())
		{
			const ssize_t result = write(file, text.data() + offset, text.size() - offset);
			if (result < 0 && errno == EINTR)
				continue;
			written = result > 0;
			offset += written ? static_cast<size_t>(result) : 0;
		}
		return close(file) == 0 && written;
#endif
	}

	std::string AgentServer::GenerateSessionToken()
	{
		std::array<uint8_t, 32> bytes {};
		if (!ReadSecureRandom(bytes.data(), bytes.size()))
			return {};
		static constexpr char c_Hex[] = "0123456789abcdef";
		std::string token;
		token.reserve(bytes.size() * 2);
		for (uint8_t byte : bytes)
		{
			token.push_back(c_Hex[byte >> 4]);
			token.push_back(c_Hex[byte & 0x0F]);
		}
		return token;
	}

	bool AgentServer::LooksLikeHttp(std::string_view firstLine)
	{
		static constexpr std::string_view c_Methods[] = { "GET ", "POST ", "PUT ", "PATCH ", "DELETE ", "HEAD ", "OPTIONS ", "CONNECT ", "TRACE " };
		const size_t start = firstLine.find_first_not_of(" \t\r");
		if (start == std::string_view::npos)
			return false;
		const std::string_view line = firstLine.substr(start);
		for (std::string_view method : c_Methods)
		{
			if (line.substr(0, method.size()) == method)
				return true;
		}
		return line.find("HTTP/1.") != std::string_view::npos || line.find("HTTP/2") != std::string_view::npos;
	}

	bool AgentServer::TokensMatch(std::string_view given, std::string_view expected)
	{
		if (expected.empty() || given.size() != expected.size())
			return false;
		unsigned char difference = 0;
		for (size_t i = 0; i < expected.size(); i++)
			difference |= static_cast<unsigned char>(given[i] ^ expected[i]);
		return difference == 0;
	}

	void AgentServer::Stop()
	{
		if (!m_Running)
			return;
		m_Running = false;
		if (m_ListenSocket != -1)
		{
			CloseSocket(static_cast<SocketHandle>(m_ListenSocket));
			m_ListenSocket = -1;
		}
		if (m_Thread.joinable())
			m_Thread.join();

		// Fail anything still queued so client threads never wait forever.
		std::scoped_lock lock(m_QueueMutex);
		for (auto& request : m_Queue)
			request->Response.set_value(ErrorResponse(request->Request.value("id", Json()), "Editor is shutting down"));
		for (auto& request : m_Waiting)
			request->Response.set_value(ErrorResponse(request->Request.value("id", Json()), "Editor is shutting down"));
		m_Queue.clear();
		m_Waiting.clear();

		std::error_code error;
		std::filesystem::remove(GetSessionFilePath(), error);
#if defined(_WIN32)
		WSACleanup();
#endif
	}

	void AgentServer::ServerThread()
	{
		while (m_Running)
		{
			const SocketHandle listenSocket = static_cast<SocketHandle>(m_ListenSocket);
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(listenSocket, &readSet);
			timeval timeout { 0, 200000 };
			const int ready = select(static_cast<int>(listenSocket) + 1, &readSet, nullptr, nullptr, &timeout);
			if (!m_Running)
				break;
			if (ready <= 0)
				continue;

			SocketHandle client = accept(listenSocket, nullptr, nullptr);
			if (client == c_InvalidSocket)
				continue;

			// Each client gets its own thread, so an idle one doesn't hold up the others; finished
			// ones are joined here, and a client past the limit is turned away.
			std::erase_if(m_Connections, [](Connection& connection)
			{
				if (!connection.Finished->load())
					return false;
				connection.Thread.join();
				return true;
			});
			if (m_Connections.size() >= static_cast<size_t>(MaxConnections))
			{
				CloseSocket(client);
				continue;
			}
			auto finished = std::make_shared<std::atomic<bool>>(false);
			m_Connections.push_back({ std::thread([this, client, finished]
			{
				HandleConnection(static_cast<intptr_t>(client));
				CloseSocket(client);
				finished->store(true);
			}), finished });
		}

		// Stop: every connection sees m_Running go false within a select timeout.
		for (Connection& connection : m_Connections)
			connection.Thread.join();
		m_Connections.clear();
	}

	void AgentServer::HandleConnection(intptr_t socketHandle)
	{
		const SocketHandle client = static_cast<SocketHandle>(socketHandle);
		std::string buffer;
		char chunk[4096];
		bool firstLine = true;
		auto lineStarted = std::chrono::steady_clock::now();
		while (m_Running)
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(client, &readSet);
			timeval timeout { 0, 200000 };
			const int ready = select(static_cast<int>(client) + 1, &readSet, nullptr, nullptr, &timeout);
			if (ready < 0)
				return;
			// A half-sent line that doesn't finish in time is dropped, so it can't hold a connection.
			if (!buffer.empty() && std::chrono::steady_clock::now() - lineStarted > c_LineTimeout)
				return;
			if (ready == 0)
				continue;

			const auto received = recv(client, chunk, sizeof(chunk), 0);
			if (received <= 0)
				return;
			if (buffer.empty())
				lineStarted = std::chrono::steady_clock::now();
			buffer.append(chunk, static_cast<size_t>(received));
			if (buffer.size() > 64 * 1024 * 1024)
				return;	// Refuse absurdly large requests

			size_t newline;
			while ((newline = buffer.find('\n')) != std::string::npos)
			{
				const std::string line = buffer.substr(0, newline);
				buffer.erase(0, newline + 1);
				lineStarted = std::chrono::steady_clock::now();
				if (line.find_first_not_of(" \r\t") == std::string::npos)
					continue;
				// A browser's request (a web page posting to this port) starts with an HTTP request
				// line: drop the connection without answering, so its body is never read as a request.
				if (std::exchange(firstLine, false) && LooksLikeHttp(line))
				{
					BW_CORE_WARN("Agent server: refused an HTTP request (a web page or browser tried to connect)");
					return;
				}
				if (!SendAll(client, Dispatch(line) + "\n"))
					return;
			}
		}
	}

	std::string AgentServer::Dispatch(const std::string& line)
	{
		Json request = Json::parse(line, nullptr, false);
		if (request.is_discarded() || !request.is_object())
			return ErrorResponse(Json(), "Request is not a JSON object");

		const Json id = request.value("id", Json());
		const auto token = request.find("token");
		if (token == request.end() || !token->is_string() || !TokensMatch(token->get_ref<const std::string&>(), m_Token))
			return ErrorResponse(id, "Invalid or missing session token");
		if (!request.contains("method") || !request["method"].is_string())
			return ErrorResponse(id, "Missing 'method'");

		m_RequestCount++;
		auto pending = std::make_shared<PendingRequest>();
		pending->Request = std::move(request);
		std::future<std::string> response = pending->Response.get_future();
		{
			std::scoped_lock lock(m_QueueMutex);
			m_Queue.push_back(pending);
		}

		// Requests may legitimately take long (exports, long waits), so wait while the server runs.
		while (m_Running)
		{
			if (response.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready)
				return response.get();
		}
		return ErrorResponse(id, "Editor is shutting down");
	}

	void AgentServer::ProcessRequests()
	{
		std::deque<std::shared_ptr<PendingRequest>> queue;
		std::vector<std::shared_ptr<PendingRequest>> finishedWaits;
		{
			std::scoped_lock lock(m_QueueMutex);
			queue.swap(m_Queue);
			for (auto it = m_Waiting.begin(); it != m_Waiting.end();)
			{
				if (--(*it)->FramesToWait <= 0)
				{
					finishedWaits.push_back(*it);
					it = m_Waiting.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		for (auto& waited : finishedWaits)
		{
			Json response;
			response["id"] = waited->Request.value("id", Json());
			response["ok"] = true;
			response["result"] = true;
			waited->Response.set_value(response.dump());
		}

		for (auto& pending : queue)
		{
			const Json& request = pending->Request;
			const Json id = request.value("id", Json());
			const std::string method = request["method"].get<std::string>();
			const Json params = request.value("params", Json::object());

			Json response;
			response["id"] = id;
			try
			{
				if (method == "wait_frames")
				{
					const Json frames = params.is_object() ? params.value("frames", Json(1)) : Json(1);
					if (!frames.is_number_integer())
						throw std::runtime_error("'frames' must be a whole number");
					pending->FramesToWait = static_cast<int>(std::clamp<int64_t>(frames.get<int64_t>(), 1, 1000000));
					std::scoped_lock lock(m_QueueMutex);
					m_Waiting.push_back(pending);
					continue;
				}
				response["result"] = m_Handler(method, params);
				response["ok"] = true;
			}
			catch (const std::exception& exception)
			{
				response["ok"] = false;
				response["error"] = exception.what();
			}
			pending->Response.set_value(response.dump());
		}
	}

}
