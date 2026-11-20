#pragma once

#include "Bellows/Serialization/JsonUtils.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Bellows {

	// Lets AI agents and tools drive the editor over a local TCP connection.
	//
	// Protocol: newline-delimited JSON. Each request is
	//   {"id": 1, "token": "<session token>", "method": "exec", "params": {"code": "bellows.editor.play()"}}
	// and is answered with
	//   {"id": 1, "ok": true, "result": ...}   or   {"id": 1, "ok": false, "error": "..."}
	//
	// The server only listens on 127.0.0.1 and requires the per-session token that is written to the
	// session file (see GetSessionFilePath; only its owner can read it). A connection that opens like
	// HTTP is dropped unanswered, so web pages can't reach it. Each connection is served on its own
	// thread; requests are executed on the main thread between frames.
	class AgentServer
	{
	public:
		// Runs on the main thread. Returns the result, or throws std::runtime_error for an error response.
		using Handler = std::function<Json(const std::string& method, const Json& params)>;

		AgentServer() = default;
		~AgentServer();

		AgentServer(const AgentServer&) = delete;
		AgentServer& operator=(const AgentServer&) = delete;

		// Tries 'port' and the next few ports. Returns false if none could be opened.
		bool Start(uint16_t port, Handler handler);
		void Stop();

		// Executes queued requests. Call once per frame on the main thread.
		void ProcessRequests();

		bool IsRunning() const { return m_Running; }
		uint16_t GetPort() const { return m_Port; }
		const std::string& GetToken() const { return m_Token; }
		uint64_t GetRequestCount() const { return m_RequestCount; }

		static std::filesystem::path GetSessionFilePath();

		// 32 bytes from the system's secure random source, as 64 hex digits; empty if it can't be read.
		static std::string GenerateSessionToken();
		// Whether a connection's first line is an HTTP request line (what a browser sends first).
		static bool LooksLikeHttp(std::string_view firstLine);
		// Compares in time that depends only on the lengths, so a guess can't be timed letter by letter.
		static bool TokensMatch(std::string_view given, std::string_view expected);

		static constexpr int MaxConnections = 8;
	private:
		struct PendingRequest
		{
			Json Request;
			std::promise<std::string> Response;
			int FramesToWait = -1;		// For "wait_frames": answered after this many frames
		};

		struct Connection
		{
			std::thread Thread;
			std::shared_ptr<std::atomic<bool>> Finished;
		};

		void ServerThread();
		void HandleConnection(intptr_t socket);
		std::string Dispatch(const std::string& line);
		bool WriteSessionFile() const;
	private:
		Handler m_Handler;
		std::thread m_Thread;
		std::vector<Connection> m_Connections;		// Only the server thread touches these
		std::atomic<bool> m_Running = false;
		intptr_t m_ListenSocket = -1;
		uint16_t m_Port = 0;
		std::string m_Token;
		std::atomic<uint64_t> m_RequestCount = 0;

		std::mutex m_QueueMutex;
		std::deque<std::shared_ptr<PendingRequest>> m_Queue;
		std::vector<std::shared_ptr<PendingRequest>> m_Waiting;
	};

}
