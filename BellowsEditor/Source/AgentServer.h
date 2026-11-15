#pragma once

#include "Bellows/Serialization/JsonUtils.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>

namespace Bellows {

	// Lets AI agents and tools drive the editor over a local TCP connection.
	//
	// Protocol: newline-delimited JSON. Each request is
	//   {"id": 1, "token": "<session token>", "method": "exec", "params": {"code": "bellows.editor.play()"}}
	// and is answered with
	//   {"id": 1, "ok": true, "result": ...}   or   {"id": 1, "ok": false, "error": "..."}
	//
	// The server only listens on 127.0.0.1 and requires the per-session token that is written to the
	// session file (see GetSessionFilePath). Requests are executed on the main thread between frames.
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
	private:
		struct PendingRequest
		{
			Json Request;
			std::promise<std::string> Response;
			int FramesToWait = -1;		// For "wait_frames": answered after this many frames
		};

		void ServerThread();
		void HandleConnection(intptr_t socket);
		std::string Dispatch(const std::string& line);
	private:
		Handler m_Handler;
		std::thread m_Thread;
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
