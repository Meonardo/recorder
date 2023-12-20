#pragma once

#include <mutex>
#include <string>
#include <atomic>
#include <memory>

class WebSocketSession;
typedef std::shared_ptr<WebSocketSession> SessionPtr;

class WebSocketSession {
public:
	inline std::string RemoteAddress() {
		std::lock_guard<std::mutex> lock(remoteAddressMutex);
		return remoteAddress;
	}
	inline void SetRemoteAddress(std::string address) {
		std::lock_guard<std::mutex> lock(remoteAddressMutex);
		remoteAddress = address;
	}

	inline uint64_t ConnectedAt() { return connectedAt; }
	inline void SetConnectedAt(uint64_t at) { connectedAt = at; }

	inline uint64_t IncomingMessages() { return incomingMessages; }
	inline void IncrementIncomingMessages() { incomingMessages++; }

	inline uint64_t OutgoingMessages() { return outgoingMessages; }
	inline void IncrementOutgoingMessages() { outgoingMessages++; }

	inline uint8_t Encoding() { return encoding; }
	inline void SetEncoding(uint8_t encoding) { encoding = encoding; }

	std::mutex OperationMutex;

private:
	std::mutex remoteAddressMutex;
	std::string remoteAddress;
	std::atomic<uint64_t> connectedAt = 0;
	std::atomic<uint64_t> incomingMessages = 0;
	std::atomic<uint64_t> outgoingMessages = 0;
	std::atomic<uint8_t> encoding = 0;
};
