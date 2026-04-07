#include <rlbot/Server.h>

#include "Log.h"
#include "Message.h"
#include "Socket.h"
#include "TracyHelper.h"

#ifdef _WIN32
#include "WsaData.h"
#endif

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <deque>
#include <thread>
#include <unordered_map>

using namespace rlbot;
using namespace rlbot::detail;

using namespace std::chrono_literals;

namespace
{
template <typename T>
rlbot::flat::CorePacketT buildCorePacket (T &&packet_) noexcept
{
	rlbot::flat::CorePacketT corePacket;
	corePacket.message.Set (std::move (packet_));
	return corePacket;
}

struct ClientContext
{
	/// @brief Client connection ID
	std::uint64_t connectionId;

	/// @brief Client socket
	UniqueSocket socket;

	/// @brief Input buffer
	Pool<Buffer>::Ref inputBuffer;
	/// @brief Input buffer size
	std::size_t inputBufferSize = 0;

	/// @brief Outgoing buffers
	std::deque<Pool<flatbuffers::FlatBufferBuilder>::Ref> outgoingBuffers;

	/// @brief Output buffer
	Pool<Buffer>::Ref outputBuffer;
	/// @brief Output buffer size
	std::uint16_t outputBufferSize = 0;
	/// @brief Output buffer offset
	std::size_t outputBufferOffset = 0;

	/// @brief Read message from client socket
	bool read (Message &message_) noexcept;

	/// @brief Write message to client socket
	bool write () noexcept;
};

bool ClientContext::read (Message &message_) noexcept
{
	if (inputBufferSize < Message::HEADER_SIZE)
	{
		auto const rc = socket->read (
		    inputBuffer->data () + inputBufferSize, Message::HEADER_SIZE - inputBufferSize);
		if (rc <= 0)
		{
			if (rc < 0 && Socket::lastError () != EWOULDBLOCK)
			{
				error ("Failed to read from client %" PRIu64 ": %d\n",
				    connectionId,
				    Socket::lastError ());
				return false;
			}
			else if (rc == 0)
			{
				info ("Client %" PRIu64 " disconnected\n", connectionId);
				return false;
			}
		}
		else
			inputBufferSize += rc;

		if (inputBufferSize < Message::HEADER_SIZE)
			return true; // need more data
	}

	auto message = Message (inputBuffer);
	if (message.sizeWithHeader () > inputBuffer->size ()) [[unlikely]]
	{
		error ("Message size exceeds input buffer size for client %" PRIu64 "\n", connectionId);
		return false;
	}

	auto const rc = socket->read (
	    inputBuffer->data () + inputBufferSize, message.sizeWithHeader () - inputBufferSize);
	if (rc <= 0)
	{
		if (rc < 0 && Socket::lastError () != EWOULDBLOCK)
		{
			error (
			    "Failed to read from client %" PRIu64 ": %d\n", connectionId, Socket::lastError ());
			return false;
		}
		else if (rc == 0)
		{
			info ("Client %" PRIu64 " disconnected\n", connectionId);
			return false;
		}

		return true; // need more data
	}

	inputBufferSize += rc;
	if (message.sizeWithHeader () > inputBufferSize) [[unlikely]]
		return true; // need more data

	message_ = std::move (message);

	inputBuffer.reset ();
	inputBufferSize = 0;

	return true;
}

bool ClientContext::write () noexcept
{
	assert (!outgoingBuffers.empty ());

	auto &fbb = outgoingBuffers.front ();
	if (!fbb) [[unlikely]]
		return false; // signal to remove client

	auto const size = fbb->GetSize ();
	assert (outputBufferOffset < size);

	if (outputBufferSize == 0)
	{
		outputBufferSize = Message::HEADER_SIZE + size;
		assert (outputBuffer->size () >= outputBufferSize);

		// encode header
		outputBuffer->operator[] (0) = size >> CHAR_BIT;
		outputBuffer->operator[] (1) = size;

		// copy payload
		if (size > 0) [[likely]]
			std::memcpy (
			    &outputBuffer->operator[] (Message::HEADER_SIZE), fbb->GetBufferPointer (), size);
	}

	auto const rc = socket->write (
	    &outputBuffer->operator[] (outputBufferOffset), outputBufferSize - outputBufferOffset);
	if (rc <= 0)
	{
		if (rc < 0 && Socket::lastError () != EWOULDBLOCK)
		{
			error (
			    "Failed to write to client %" PRIu64 ": %d\n", connectionId, Socket::lastError ());
			return false;
		}
		else if (rc == 0)
		{
			info ("Client %" PRIu64 " disconnected\n", connectionId);
			return false;
		}

		return true; // need to write more data
	}

	outputBufferOffset += rc;
	if (outputBufferOffset >= outputBufferSize)
	{
		outputBufferSize   = 0;
		outputBufferOffset = 0;
		outgoingBuffers.pop_front ();
	}

	return true;
}
}

///////////////////////////////////////////////////////////////////////////
class rlbot::detail::ServerImpl
{
public:
	/// @brief Request service thread to terminate
	void terminate () noexcept;

	/// @brief Join service thread
	void join () noexcept;

	/// @brief Service thread
	std::thread serviceThread;
	/// @brief Signal to quit
	std::atomic_bool quit = false;
	/// @brief Whether service thread is running
	std::atomic_bool running = false;

#ifdef _WIN32
	/// @brief WSA data
	WsaData wsaData;
#endif

	/// @brief Listen socket
	UniqueSocket listenSocket{};

	/// @brief Internal read socket
	UniqueSocket internalReadSocket{};

	/// @brief Internal write socket
	UniqueSocket internalWriteSocket{};

	/// @brief Clients connected to the server
	std::unordered_map<std::uint64_t, ClientContext> clients{};
	/// @brief Next connection ID
	std::uint64_t nextConnectionId = 0;

	/// @brief Buffer pool
	std::array<std::shared_ptr<Pool<Buffer>>, 4> bufferPools;
	/// @brief Buffer pool index for round-robining
	std::atomic_uint bufferPoolIndex = 0;

	/// @brief Flatbuffer builder pool
	std::shared_ptr<Pool<flatbuffers::FlatBufferBuilder>> fbbPool =
	    Pool<flatbuffers::FlatBufferBuilder>::create ("FBB");
};

void ServerImpl::terminate () noexcept
{
}

void ServerImpl::join () noexcept
{
}

///////////////////////////////////////////////////////////////////////////
Server::~Server () noexcept
{
	join ();
}

Server::Server () noexcept : m_impl (new detail::ServerImpl{})
{
}

bool Server::run (char const *const host_, char const *const service_) noexcept
{
	if (m_impl->running.load (std::memory_order_relaxed))
	{
		error ("Already running\n");
		return false;
	}

#ifdef _WIN32
	if (!m_impl->wsaData.init ())
		return false;
#endif

	// resolve host/port
	SockAddr addr;
	if (!SockAddr::resolve (host_, service_, addr))
		return false;

	auto listenSock = Socket::create (addr.domain (), Socket::eStream);
	if (!listenSock || !listenSock->setReuseAddress () || !listenSock->setReusePort () ||
	    !listenSock->setNoDelay () || !listenSock->bind (addr) || !listenSock->listen (5))
		return false;

	auto writeSock = Socket::create (addr.domain (), Socket::eStream);
	if (!writeSock || !writeSock->setNoDelay () || !writeSock->setNonBlocking ())
		return false;

	if (addr.domain () == SockAddr::Domain::IPv6 && !writeSock->bind (SockAddr::LoopbackIPv6))
		return false;
	else if (!writeSock->bind (SockAddr::LoopbackIPv4))
		return false;

	if (addr == SockAddr::AnyIPv6.withPort (addr.port ()))
		addr = SockAddr::LoopbackIPv6.withPort (addr.port ());
	else if (addr == SockAddr::AnyIPv4.withPort (addr.port ()))
		addr = SockAddr::LoopbackIPv4.withPort (addr.port ());

	if (!writeSock->connect (addr))
	{
		auto const err = Socket::lastError ();
		if (err != EINPROGRESS && err != EWOULDBLOCK)
			return false;
	}

	// reset buffer pools
	for (unsigned i = 0; auto &pool : m_impl->bufferPools)
		pool = Pool<Buffer>::create ("Buffer " + std::to_string (i++));

	std::unordered_map<std::uint64_t, ClientContext> clients;
	while (true)
	{
		auto readSock = listenSock->accept ();
		if (!readSock || !readSock->setNoDelay () || !readSock->setNonBlocking ())
			return false;

		if (readSock->peerName () == writeSock->sockName ())
		{
			m_impl->listenSocket        = std::move (listenSock);
			m_impl->internalReadSocket  = std::move (readSock);
			m_impl->internalWriteSocket = std::move (writeSock);
			break;
		}

		readSock->setRecvBufferSize (1024 * 1024);
		readSock->setSendBufferSize (1024 * 1024);

		auto const id = ++m_impl->nextConnectionId;

		auto &client        = clients[id];
		client.connectionId = id;
		client.socket       = std::move (readSock);
		client.outputBuffer =
		    m_impl->bufferPools[m_impl->bufferPoolIndex++ % m_impl->bufferPools.size ()]
		        ->getObject ();
	}

	m_impl->clients = std::move (clients);

	m_impl->serviceThread = std::thread (&Server::serviceThread, this);

	m_impl->running.store (true, std::memory_order_relaxed);

	return true;
}

bool Server::running () const noexcept
{
	return m_impl->running.load (std::memory_order_relaxed);
}

void Server::terminate () noexcept
{
	if (m_impl->running.load (std::memory_order_relaxed))
	{
		m_impl->quit.store (true, std::memory_order_relaxed);

		char data = 0;
		m_impl->internalWriteSocket->write (&data, sizeof (data));
	}
}

void Server::join () noexcept
{
	if (m_impl->running.load (std::memory_order_relaxed))
		m_impl->serviceThread.join ();

	m_impl->listenSocket.reset ();
	m_impl->internalReadSocket.reset ();
	m_impl->internalWriteSocket.reset ();
	m_impl->clients.clear ();

	m_impl->running.store (false, std::memory_order_relaxed);
}

void Server::sendCorePacket (rlbot::flat::CorePacketT const &packet_,
    std::uint64_t const connectionId_) noexcept
{
	debug ("Enqueue %s for client %" PRIu64 "\n",
	    rlbot::flat::EnumNameCoreMessage (packet_.message.type),
	    connectionId_);

	if (connectionId_ == 0)
	{
		if (m_impl->clients.empty ())
			return;

		auto fbb = m_impl->fbbPool->getObject ();
		fbb->Clear ();
		fbb->Finish (rlbot::flat::CreateCorePacket (*fbb, &packet_));

		for (auto &[id, client] : m_impl->clients)
			client.outgoingBuffers.emplace_back (fbb);
		return;
	}

	auto it = m_impl->clients.find (connectionId_);
	if (it == std::end (m_impl->clients))
		return;

	auto fbb = m_impl->fbbPool->getObject ();
	fbb->Clear ();
	fbb->Finish (rlbot::flat::CreateCorePacket (*fbb, &packet_));

	it->second.outgoingBuffers.emplace_back (fbb);
}

void Server::sendDisconnectSignal (rlbot::flat::DisconnectSignalT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue DisconnectSignal", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);

	// signal to disconnect client(s) after sending the packet
	if (connectionId_ == 0)
	{
		for (auto &[id, client] : m_impl->clients)
			client.outgoingBuffers.emplace_back ();
	}
	else
	{
		auto it = m_impl->clients.find (connectionId_);
		if (it == std::end (m_impl->clients))
			return;

		it->second.outgoingBuffers.emplace_back ();
	}
}

void Server::sendGamePacket (rlbot::flat::GamePacketT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue GamePacket", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendFieldInfo (rlbot::flat::FieldInfoT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue FieldInfo", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendMatchConfiguration (rlbot::flat::MatchConfigurationT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue MatchConfiguration", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendMatchComm (rlbot::flat::MatchCommT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue MatchComm", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendBallPrediction (rlbot::flat::BallPredictionT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue BallPrediction", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendControllableTeamInfo (rlbot::flat::ControllableTeamInfoT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue ControllableTeamInfo", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendRenderingStatus (rlbot::flat::RenderingStatusT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue RenderingStatus", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendPingRequest (rlbot::flat::PingRequestT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue PingRequest", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::sendPingResponse (rlbot::flat::PingResponseT packet_,
    std::uint64_t const connectionId_) noexcept
{
	ZoneScopedNS ("enqueue PingResponse", 16);
	sendCorePacket (buildCorePacket (std::move (packet_)), connectionId_);
}

void Server::removeConnection (std::uint64_t const connectionId_) noexcept
{
	auto const it = m_impl->clients.find (connectionId_);
	if (it == std::end (m_impl->clients))
		return;

	handleConnectionRemoved (connectionId_);
	m_impl->clients.erase (it);
}

void Server::handleMessage (std::uint64_t const connectionId_, detail::Message &message_) noexcept
{
	auto const packet = message_.interfacePacket (true);
	if (!packet) [[unlikely]]
		error ("Invalid interface packet received\n");
	else
		handleInterfacePacket (connectionId_, packet);
}

void Server::handleInterfacePacket (std::uint64_t const connectionId_,
    rlbot::flat::InterfacePacket const *const packet_) noexcept
{
	debug ("Received %s from client %" PRIu64 "\n",
	    rlbot::flat::EnumNameInterfaceMessage (packet_->message_type ()),
	    connectionId_);

	switch (packet_->message_type ())
	{
	case rlbot::flat::InterfaceMessage::NONE:
		error ("Received packet with NONE message type\n");
		return;

	case rlbot::flat::InterfaceMessage::DisconnectSignal:
		handleDisconnectSignal (connectionId_, packet_->message_as_DisconnectSignal ());
		break;

	case rlbot::flat::InterfaceMessage::StartCommand:
		handleStartCommand (connectionId_, packet_->message_as_StartCommand ());
		break;

	case rlbot::flat::InterfaceMessage::MatchConfiguration:
		handleMatchConfiguration (connectionId_, packet_->message_as_MatchConfiguration ());
		break;

	case rlbot::flat::InterfaceMessage::PlayerInput:
		handlePlayerInput (connectionId_, packet_->message_as_PlayerInput ());
		break;

	case rlbot::flat::InterfaceMessage::DesiredGameState:
		handleDesiredGameState (connectionId_, packet_->message_as_DesiredGameState ());
		break;

	case rlbot::flat::InterfaceMessage::RenderGroup:
		handleRenderGroup (connectionId_, packet_->message_as_RenderGroup ());
		break;

	case rlbot::flat::InterfaceMessage::RemoveRenderGroup:
		handleRemoveRenderGroup (connectionId_, packet_->message_as_RemoveRenderGroup ());
		break;

	case rlbot::flat::InterfaceMessage::MatchComm:
		handleMatchComm (connectionId_, packet_->message_as_MatchComm ());
		break;

	case rlbot::flat::InterfaceMessage::ConnectionSettings:
		handleConnectionSettings (connectionId_, packet_->message_as_ConnectionSettings ());
		break;

	case rlbot::flat::InterfaceMessage::StopCommand:
		handleStopCommand (connectionId_, packet_->message_as_StopCommand ());
		break;

	case rlbot::flat::InterfaceMessage::SetLoadout:
		handleSetLoadout (connectionId_, packet_->message_as_SetLoadout ());
		break;

	case rlbot::flat::InterfaceMessage::InitComplete:
		handleInitComplete (connectionId_, packet_->message_as_InitComplete ());
		break;

	case rlbot::flat::InterfaceMessage::RenderingStatus:
		handleRenderingStatus (connectionId_, packet_->message_as_RenderingStatus ());
		break;

	case rlbot::flat::InterfaceMessage::PingRequest:
		handlePingRequest (connectionId_, packet_->message_as_PingRequest ());
		break;

	case rlbot::flat::InterfaceMessage::PingResponse:
		handlePingResponse (connectionId_, packet_->message_as_PingResponse ());
		break;
	}
}

void Server::handleDisconnectSignal (std::uint64_t const connectionId_,
    rlbot::flat::DisconnectSignal const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleStartCommand (std::uint64_t const connectionId_,
    rlbot::flat::StartCommand const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleMatchConfiguration (std::uint64_t const connectionId_,
    rlbot::flat::MatchConfiguration const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handlePlayerInput (std::uint64_t const connectionId_,
    rlbot::flat::PlayerInput const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleDesiredGameState (std::uint64_t const connectionId_,
    rlbot::flat::DesiredGameState const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleRenderGroup (std::uint64_t const connectionId_,
    rlbot::flat::RenderGroup const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleRemoveRenderGroup (std::uint64_t const connectionId_,
    rlbot::flat::RemoveRenderGroup const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleMatchComm (std::uint64_t const connectionId_,
    rlbot::flat::MatchComm const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleConnectionSettings (std::uint64_t const connectionId_,
    rlbot::flat::ConnectionSettings const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleStopCommand (std::uint64_t const connectionId_,
    rlbot::flat::StopCommand const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleSetLoadout (std::uint64_t const connectionId_,
    rlbot::flat::SetLoadout const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleInitComplete (std::uint64_t const connectionId_,
    rlbot::flat::InitComplete const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleRenderingStatus (std::uint64_t const connectionId_,
    rlbot::flat::RenderingStatus const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handleConnectionRemoved (std::uint64_t const connectionId_) noexcept
{
	(void)connectionId_;
}

void Server::handlePingRequest (std::uint64_t const connectionId_,
    rlbot::flat::PingRequest const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Server::handlePingResponse (std::uint64_t const connectionId_,
    rlbot::flat::PingResponse const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

bool Server::loop () noexcept
{
	return true;
}

void Server::serviceThread () noexcept
{
	info ("Service thread started\n");

	while (!m_impl->quit.load (std::memory_order_relaxed))
	{
		if (!loop ())
			break;

		std::vector<Socket::PollInfo> pollInfos;
		pollInfos.reserve (m_impl->clients.size () + 2);

		pollInfos.emplace_back (Socket::PollInfo{std::ref (*m_impl->listenSocket), POLLIN});
		pollInfos.emplace_back (Socket::PollInfo{std::ref (*m_impl->internalReadSocket), POLLIN});

		for (auto const &[id, client] : m_impl->clients)
		{
			if (client.outgoingBuffers.empty ())
				pollInfos.emplace_back (Socket::PollInfo{std::ref (*client.socket), POLLIN});
			else
				pollInfos.emplace_back (
				    Socket::PollInfo{std::ref (*client.socket), POLLIN | POLLOUT});
		}

		auto const nfds = Socket::poll (pollInfos.data (), pollInfos.size (), 1ms);
		if (nfds < 0)
		{
			error ("Failed to poll sockets: %d\n", Socket::lastError ());
			continue;
		}

		if (nfds == 0)
			continue;

		for (auto const &pollInfo : pollInfos)
		{
			if (!pollInfo.revents)
				continue;

			auto &socket = pollInfo.socket.get ();
			if (&socket == m_impl->listenSocket.get () && (pollInfo.revents & POLLIN))
			{
				auto readSock = socket.accept ();
				if (!readSock || !readSock->setNoDelay () || !readSock->setNonBlocking ())
					continue;

				readSock->setRecvBufferSize (1024 * 1024);
				readSock->setSendBufferSize (1024 * 1024);

				auto const id = ++m_impl->nextConnectionId;

				auto &client        = m_impl->clients[id];
				client.connectionId = id;
				client.socket       = std::move (readSock);
				client.outputBuffer =
				    m_impl->bufferPools[m_impl->bufferPoolIndex++ % m_impl->bufferPools.size ()]
				        ->getObject ();

				info ("Accepted client %" PRIu64 " [%s]:%u\n",
				    id,
				    client.socket->peerName ().name (),
				    client.socket->peerName ().port ());
				continue;
			}

			if (&socket == m_impl->internalReadSocket.get () && (pollInfo.revents & POLLIN))
			{
				char data;
				m_impl->internalReadSocket->read (&data, sizeof (data));
				continue;
			}

			auto it = std::ranges::find (m_impl->clients, &socket, [&] (auto const &pair) {
				return pair.second.socket.get ();
			});

			if (it == std::end (m_impl->clients))
				continue;

			auto &client = it->second;

			if (pollInfo.revents & POLLIN)
			{
				if (!client.inputBuffer)
				{
					client.inputBuffer =
					    m_impl->bufferPools[m_impl->bufferPoolIndex++ % m_impl->bufferPools.size ()]
					        ->getObject ();
					client.inputBufferSize = 0;
				}

				Message message;
				if (!client.read (message))
				{
					removeConnection (client.connectionId);
					continue;
				}

				if (message)
					handleMessage (client.connectionId, message);
			}

			if (pollInfo.revents & POLLOUT)
			{
				if (!client.write ())
				{
					removeConnection (client.connectionId);
					continue;
				}
			}
		}
	}

	info ("Service thread stopped\n");
}
