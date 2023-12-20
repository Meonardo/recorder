#include <chrono>
#include <thread>
#include <functional>

#include <QDateTime>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QRunnable>

#include "websocket.h"

#define SERVER_PORT 8359

namespace compat {
// Reimplement QRunnable for std::function. Retrocompatability for Qt < 5.15
class StdFunctionRunnable : public QRunnable {
	std::function<void()> cb;

public:
	StdFunctionRunnable(std::function<void()> func) : cb(std::move(func)) {}
	void run() override { cb(); }
};

QRunnable* CreateFunctionRunnable(std::function<void()> func) {
	return new StdFunctionRunnable(std::move(func));
}
} // namespace compat

static std::string GetLocalAddress() {
	std::vector<QString> validAddresses;
	for (auto address : QNetworkInterface::allAddresses()) {
		// Exclude addresses which won't work
		if (address == QHostAddress::LocalHost)
			continue;
		else if (address == QHostAddress::LocalHostIPv6)
			continue;
		else if (address.isLoopback())
			continue;
		else if (address.isLinkLocal())
			continue;
		else if (address.isNull())
			continue;

		validAddresses.push_back(address.toString());
	}

	// Return early if no valid addresses were found
	if (validAddresses.size() == 0)
		return "0.0.0.0";

	std::vector<std::pair<QString, uint8_t>> preferredAddresses;
	for (auto address : validAddresses) {
		// Attribute a priority (0 is best) to the address to choose the best picks
		if (address.startsWith("192.168.1.") ||
		    address.startsWith(
		      "192.168.0.")) { // Prefer common consumer router network prefixes
			if (address.startsWith("192.168.56."))
				preferredAddresses.push_back(
				  std::make_pair(address,
						 255)); // Ignore virtualbox default
			else
				preferredAddresses.push_back(std::make_pair(address, 0));
		} else if (address.startsWith(
			     "172.16.")) { // Slightly less common consumer router network prefixes
			preferredAddresses.push_back(std::make_pair(address, 1));
		} else if (address.startsWith(
			     "192.168.")) { // Slightly less common consumer router network prefixes
			preferredAddresses.push_back(std::make_pair(address, 2));
		} else if (address.startsWith(
			     "10.")) { // Even less common consumer router network prefixes
			preferredAddresses.push_back(std::make_pair(address, 3));
		} else { // Set all other addresses to equal priority
			preferredAddresses.push_back(std::make_pair(address, 255));
		}
	}

	// Sort by priority
	std::sort(preferredAddresses.begin(), preferredAddresses.end(),
		  [=](std::pair<QString, uint8_t> a, std::pair<QString, uint8_t> b) {
			  return a.second < b.second;
		  });

	// Return highest priority address
	return preferredAddresses[0].first.toStdString();
}

WebSocketServer::WebSocketServer() : QObject(nullptr) {
	server.get_alog().clear_channels(websocketpp::log::alevel::all);
	server.get_elog().clear_channels(websocketpp::log::elevel::all);
	server.init_asio();

	server.set_validate_handler(websocketpp::lib::bind(&WebSocketServer::onValidate, this,
							   websocketpp::lib::placeholders::_1));
	server.set_open_handler(websocketpp::lib::bind(&WebSocketServer::onOpen, this,
						       websocketpp::lib::placeholders::_1));
	server.set_close_handler(websocketpp::lib::bind(&WebSocketServer::onClose, this,
							websocketpp::lib::placeholders::_1));
	server.set_message_handler(websocketpp::lib::bind(&WebSocketServer::onMessage, this,
							  websocketpp::lib::placeholders::_1,
							  websocketpp::lib::placeholders::_2));
}

WebSocketServer::~WebSocketServer() {
	if (server.is_listening())
		Stop();
}

void WebSocketServer::ServerRunner() {
	blog(LOG_INFO, "[WebSocketServer::ServerRunner] IO thread started.");
	try {
		server.run();
	} catch (websocketpp::exception const& e) {
		blog(LOG_ERROR,
		     "[WebSocketServer::ServerRunner] websocketpp instance returned an error: %s",
		     e.what());
	} catch (const std::exception& e) {
		blog(LOG_ERROR,
		     "[WebSocketServer::ServerRunner] websocketpp instance returned an error: %s",
		     e.what());
	} catch (...) {
		blog(LOG_ERROR,
		     "[WebSocketServer::ServerRunner] websocketpp instance returned an error");
	}
	blog(LOG_INFO, "[WebSocketServer::ServerRunner] IO thread exited.");
}

void WebSocketServer::Start() {
	if (server.is_listening()) {
		blog(
		  LOG_WARNING,
		  "[WebSocketServer::Start] Call to Start() but the server is already listening.");
		return;
	}

	// Set log levels if debug is enabled
	server.get_alog().clear_channels(websocketpp::log::alevel::all);
	server.get_elog().clear_channels(websocketpp::log::elevel::all);

	server.reset();

	websocketpp::lib::error_code errorCode;
	blog(LOG_INFO, "[WebSocketServer::Start] Locked to IPv4 bindings");
	server.listen(websocketpp::lib::asio::ip::tcp::v4(), SERVER_PORT, errorCode);

	if (errorCode) {
		std::string errorCodeMessage = errorCode.message();
		blog(LOG_INFO, "[WebSocketServer::Start] Listen failed: %s",
		     errorCodeMessage.c_str());
		return;
	}

	server.start_accept();

	blog(
	  LOG_INFO,
	  "[WebSocketServer::Start] Server started successfully on port %d. Possible connect address: %s",
	  SERVER_PORT, GetLocalAddress().c_str());

	serverThread = std::thread(&WebSocketServer::ServerRunner, this);
}

void WebSocketServer::Stop() {
	if (!server.is_listening()) {
		blog(LOG_WARNING,
		     "[WebSocketServer::Stop] Call to Stop() but the server is not listening.");
		return;
	}

	server.stop_listening();

	std::unique_lock<std::mutex> lock(sessionMutex);
	for (auto const& [hdl, session] : sessions) {
		websocketpp::lib::error_code errorCode;
		server.pause_reading(hdl, errorCode);
		if (errorCode) {
			blog(LOG_INFO, "[WebSocketServer::Stop] Error: %s",
			     errorCode.message().c_str());
			continue;
		}

		server.close(hdl, websocketpp::close::status::going_away, "Server stopping.",
			     errorCode);
		if (errorCode) {
			blog(LOG_INFO, "[WebSocketServer::Stop] Error: %s",
			     errorCode.message().c_str());
			continue;
		}
	}
	lock.unlock();

	threadPool.waitForDone();

	// This can delay the thread that it is running on. Bad but kinda required.
	while (sessions.size() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));

	serverThread.join();

	blog(LOG_INFO, "[WebSocketServer::Stop] Server stopped successfully");
}

void WebSocketServer::InvalidateSession(websocketpp::connection_hdl hdl) {
	blog(LOG_INFO, "[WebSocketServer::InvalidateSession] Invalidating a session.");

	websocketpp::lib::error_code errorCode;
	server.pause_reading(hdl, errorCode);
	if (errorCode) {
		blog(LOG_INFO, "[WebSocketServer::InvalidateSession] Error: %s",
		     errorCode.message().c_str());
		return;
	}

	server.close(hdl, WebSocketCloseCode::SessionInvalidated,
		     "Your session has been invalidated.", errorCode);
	if (errorCode) {
		blog(LOG_INFO, "[WebSocketServer::InvalidateSession] Error: %s",
		     errorCode.message().c_str());
		return;
	}
}

std::vector<WebSocketSessionState> WebSocketServer::GetWebSocketSessions() {
	std::vector<WebSocketSessionState> webSocketSessions;

	std::unique_lock<std::mutex> lock(sessionMutex);
	for (auto& [hdl, session] : sessions) {
		uint64_t connectedAt = session->ConnectedAt();
		uint64_t incomingMessages = session->IncomingMessages();
		uint64_t outgoingMessages = session->OutgoingMessages();
		std::string remoteAddress = session->RemoteAddress();

		webSocketSessions.emplace_back(WebSocketSessionState{
		  hdl, remoteAddress, connectedAt, incomingMessages, outgoingMessages});
	}
	lock.unlock();

	return webSocketSessions;
}

void WebSocketServer::SendMessageToClient(const WebSocketSessionState& state, const char* msg) {
	std::unique_lock<std::mutex> lock(sessionMutex);
	SessionPtr session;
	try {
		session = sessions.at(state.hdl);
	} catch (const std::out_of_range& oor) {
		UNUSED_PARAMETER(oor);
		return;
	}
	lock.unlock();

	session->IncrementOutgoingMessages();

	websocketpp::lib::error_code errorCode;
	server.send(state.hdl, msg, websocketpp::frame::opcode::text, errorCode);
	if (errorCode) {
		blog(LOG_INFO, "[WebSocketServer::SendMessageToClient] Error: %s",
		     errorCode.message().c_str());
		return;
	}
}

bool WebSocketServer::onValidate(websocketpp::connection_hdl hdl) {
	return true;
}

void WebSocketServer::onOpen(websocketpp::connection_hdl hdl) {
	auto conn = server.get_con_from_hdl(hdl);

	// Build new session
	std::unique_lock<std::mutex> lock(sessionMutex);
	SessionPtr session = sessions[hdl] = std::make_shared<WebSocketSession>();
	std::unique_lock<std::mutex> sessionLock(session->OperationMutex);
	lock.unlock();

	// Configure session details
	session->SetRemoteAddress(conn->get_remote_endpoint());
	session->SetConnectedAt(QDateTime::currentSecsSinceEpoch());

	sessionLock.unlock();

	// Build SessionState object for signal
	WebSocketSessionState state;
	state.hdl = hdl;
	state.remoteAddress = session->RemoteAddress();
	state.connectedAt = session->ConnectedAt();
	state.incomingMessages = session->IncomingMessages();
	state.outgoingMessages = session->OutgoingMessages();

	// Emit signals
	emit ClientConnected(state);

	// Log connection
	blog(LOG_INFO, "[WebSocketServer::onOpen] New WebSocket client has connected from %s",
	     session->RemoteAddress().c_str());

	session->IncrementOutgoingMessages();
}

void WebSocketServer::onClose(websocketpp::connection_hdl hdl) {
	auto conn = server.get_con_from_hdl(hdl);

	// Get info from the session and then delete it
	std::unique_lock<std::mutex> lock(sessionMutex);
	SessionPtr session = sessions[hdl];
	uint64_t connectedAt = session->ConnectedAt();
	uint64_t incomingMessages = session->IncomingMessages();
	uint64_t outgoingMessages = session->OutgoingMessages();
	std::string remoteAddress = session->RemoteAddress();
	sessions.erase(hdl);
	lock.unlock();

	// Build SessionState object for signal
	WebSocketSessionState state;
	state.hdl = hdl;
	state.remoteAddress = remoteAddress;
	state.connectedAt = connectedAt;
	state.incomingMessages = incomingMessages;
	state.outgoingMessages = outgoingMessages;

	// Emit signals
	emit ClientDisconnected(state, conn->get_local_close_code());

	// Log disconnection
	blog(
	  LOG_INFO,
	  "[WebSocketServer::onClose] WebSocket client `%s` has disconnected with code `%d` and reason: %s",
	  remoteAddress.c_str(), conn->get_local_close_code(),
	  conn->get_local_close_reason().c_str());
}

void WebSocketServer::onMessage(
  websocketpp::connection_hdl hdl,
  websocketpp::server<websocketpp::config::asio>::message_ptr message) {
	auto opCode = message->get_opcode();
	std::string payload = message->get_payload();

	threadPool.start(compat::CreateFunctionRunnable([=]() {
		std::unique_lock<std::mutex> lock(sessionMutex);
		SessionPtr session;
		try {
			session = sessions.at(hdl);
		} catch (const std::out_of_range& oor) {
			UNUSED_PARAMETER(oor);
			return;
		}
		lock.unlock();

		session->IncrementIncomingMessages();

		// Check for invalid opcode and decode
		websocketpp::lib::error_code errorCode;

		if (opCode != websocketpp::frame::opcode::text) {
			server.close(
			  hdl, WebSocketCloseCode::MessageDecodeError,
			  "Your session encoding is set to Json, but a binary message was received.",
			  errorCode);
			return;
		}

		WebSocketSessionState state;
		state.hdl = hdl;
		state.remoteAddress = session->RemoteAddress();
		state.connectedAt = session->ConnectedAt();
		state.incomingMessages = session->IncomingMessages();
		state.outgoingMessages = session->OutgoingMessages();

    blog(LOG_INFO, "[WebSocketServer::onOpen] Receive message from %s, content: %s",
      session->RemoteAddress().c_str(), payload.c_str());

		// Emit signals
		emit ClientSentMessage(state, payload);
	}));
}
