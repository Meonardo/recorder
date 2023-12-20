#pragma once

#include <mutex>

#include <QObject>
#include <QThreadPool>
#include <QString>

#include <asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "websocket-session.h"

enum WebSocketCloseCode {
	DontClose = 0,
	UnknownReason = 4000,
	MessageDecodeError = 4002,
	MissingDataField = 4003,
	InvalidDataFieldType = 4004,
	InvalidDataFieldValue = 4005,
	UnknownOpCode = 4006,
	SessionInvalidated = 4010,
	UnsupportedFeature = 4011,
};

struct WebSocketSessionState {
  websocketpp::connection_hdl hdl;
  std::string remoteAddress;
  uint64_t connectedAt;
  uint64_t incomingMessages;
  uint64_t outgoingMessages;
};
Q_DECLARE_METATYPE(WebSocketSessionState)

class WebSocketServer : public QObject {
	Q_OBJECT

public:
	WebSocketServer();
	~WebSocketServer();

	void Start();
	void Stop();
	void InvalidateSession(websocketpp::connection_hdl hdl);

	bool IsListening() { return server.is_listening(); }

	std::vector<WebSocketSessionState> GetWebSocketSessions();

	QThreadPool* GetThreadPool() { return &threadPool; }

	void SendMessageToClient(const WebSocketSessionState& state, const char* msg);

signals:
	void ClientConnected(WebSocketSessionState state);
	void ClientDisconnected(WebSocketSessionState state, uint16_t closeCode);
	void ClientSentMessage(WebSocketSessionState state, const std::string& msg);

private:
	struct ProcessResult {
		WebSocketCloseCode closeCode = DontClose;
		std::string closeReason;
		std::string result;
	};

	void ServerRunner();

	bool onValidate(websocketpp::connection_hdl hdl);
	void onOpen(websocketpp::connection_hdl hdl);
	void onClose(websocketpp::connection_hdl hdl);
	void onMessage(websocketpp::connection_hdl hdl,
		       websocketpp::server<websocketpp::config::asio>::message_ptr message);

	QThreadPool threadPool;

	std::thread serverThread;
	websocketpp::server<websocketpp::config::asio> server;

	std::mutex sessionMutex;
	std::map<websocketpp::connection_hdl, SessionPtr,
		 std::owner_less<websocketpp::connection_hdl>>
	  sessions;
};
