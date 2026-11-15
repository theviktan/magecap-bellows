#include "AgentServer.h"

#include "EditorSettings.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"

#include <random>

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <winsock2.h>
	#include <ws2tcpip.h>
	using SocketHandle = SOCKET;
	constexpr SocketHandle c_InvalidSocket = INVALID_SOCKET;
	inline void CloseSocket(SocketHandle s) { closesocket(s); }
	#define getpid _getpid
	#include <process.h>
#else
	#include <arpa/inet.h>
	#include <netinet/in.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <unistd.h>
	using SocketHandle = int;
	constexpr SocketHandle c_InvalidSocket = -1;
	inline void CloseSocket(SocketHandle s) { close(s); }
#endif

namespace Bellows {

	namespace {

		std::string GenerateToken()
		{
			std::random_device device;
			std::mt19937_64 engine(device());
			std::uniform_int_distribution<uint64_t> distribution;
			char buffer[33];
			snprintf(buffer, sizeof(buffer), "%016llx%016llx",
				static_cast<unsigned long long>(distribution(engine)), static_cast<unsigned long long>(distribution(engine)));
			return buffer;
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

		m_Handler = std::move(handler);
		m_Token = GenerateToken();
		m_Running = true;
		m_Thread = std::thread([this] { ServerThread(); });

		Json session;
		session["port"] = m_Port;
		session["token"] = m_Token;
		session["pid"] = static_cast<int64_t>(getpid());
		FileSystem::WriteTextFile(GetSessionFilePath(), session.dump(1, '\t'));

		BW_CORE_INFO("Agent server listening on 127.0.0.1:{}", m_Port);
		return true;
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
			HandleConnection(static_cast<intptr_t>(client));
			CloseSocket(client);
		}
	}

	void AgentServer::HandleConnection(intptr_t socketHandle)
	{
		const SocketHandle client = static_cast<SocketHandle>(socketHandle);
		std::string buffer;
		char chunk[4096];
		while (m_Running)
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(client, &readSet);
			timeval timeout { 0, 200000 };
			const int ready = select(static_cast<int>(client) + 1, &readSet, nullptr, nullptr, &timeout);
			if (ready < 0)
				return;
			if (ready == 0)
				continue;

			const auto received = recv(client, chunk, sizeof(chunk), 0);
			if (received <= 0)
				return;
			buffer.append(chunk, static_cast<size_t>(received));
			if (buffer.size() > 64 * 1024 * 1024)
				return;	// Refuse absurdly large requests

			size_t newline;
			while ((newline = buffer.find('\n')) != std::string::npos)
			{
				const std::string line = buffer.substr(0, newline);
				buffer.erase(0, newline + 1);
				if (line.find_first_not_of(" \r\t") == std::string::npos)
					continue;
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
		if (request.value("token", std::string()) != m_Token)
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

			if (method == "wait_frames")
			{
				pending->FramesToWait = std::max(1, params.value("frames", 1));
				std::scoped_lock lock(m_QueueMutex);
				m_Waiting.push_back(pending);
				continue;
			}

			Json response;
			response["id"] = id;
			try
			{
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
