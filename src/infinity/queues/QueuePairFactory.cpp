/**
 * Queues - Queue Pair Factory
 *
 * (c) 2018 Claude Barthels, ETH Zurich
 * Contact: claudeb@inf.ethz.ch
 *
 */

#include "QueuePairFactory.h"

#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <infinity/core/Configuration.h>
#include <infinity/utils/Debug.h>
#include <infinity/utils/Address.h>

namespace infinity {
namespace queues {

typedef struct {

        uint16_t localDeviceId = 0;
        uint32_t queuePairNumber = 0;
        uint32_t sequenceNumber = 0;
        uint32_t userDataSize = 0;

} serializedQueuePair;

QueuePairFactory::QueuePairFactory(infinity::core::Context *context) {

        this->context = context;
        this->serverSocket = -1;

}

QueuePairFactory::~QueuePairFactory() {

        if (serverSocket >= 0) {
                close(serverSocket);
        }

}

void QueuePairFactory::bindToPort(uint16_t port) {

        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        INFINITY_ASSERT(serverSocket >= 0, "[INFINITY][QUEUES][FACTORY] Cannot open server socket.\n");

        sockaddr_in serverAddress;
        memset(&(serverAddress), 0, sizeof(sockaddr_in));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(port);

        int32_t enabled = 1;
        int32_t returnValue = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        INFINITY_ASSERT(returnValue == 0, "[INFINITY][QUEUES][FACTORY] Cannot set socket option to reuse address.\n");

        returnValue = bind(serverSocket, (sockaddr *) &serverAddress, sizeof(sockaddr_in));
        INFINITY_ASSERT(returnValue == 0, "[INFINITY][QUEUES][FACTORY] Cannot bind to local address and port.\n");

        returnValue = listen(serverSocket, 128);
        INFINITY_ASSERT(returnValue == 0, "[INFINITY][QUEUES][FACTORY] Cannot listen on server socket.\n");

        char *ipAddressOfDevice = infinity::utils::Address::getIpAddressOfInterface(infinity::core::Configuration::DEFAULT_IB_DEVICE);
        INFINITY_DEBUG("[INFINITY][QUEUES][FACTORY] Accepting connections on IP address %s and port %d.\n", ipAddressOfDevice, port);
        free(ipAddressOfDevice);

}

int32_t QueuePairFactory::readFromSocket(int32_t socket, char *buffer, uint32_t size)
{
        int32_t bytesReceived = 0;
        while (size) {
                int32_t returnValue = recv(socket, buffer, size, 0);
                INFINITY_ASSERT(returnValue >= 0, "[INFINITY][QUEUES][FACTORY] Problem reading from socket: %s\n", strerror(errno));
                if ((returnValue <= 0) && (errno != EINTR)) { return bytesReceived; }
                size -= returnValue;
                buffer += returnValue;
                bytesReceived += returnValue;
        }
        return bytesReceived;
}

int32_t QueuePairFactory::sendToSocket(int32_t socket, const char *buffer, uint32_t size)
{
        int32_t bytesSent = 0;
        while (size) {
                int32_t returnValue = send(socket, buffer, size, 0);
                INFINITY_ASSERT(returnValue >= 0, "[INFINITY][QUEUES][FACTORY] Problem sending on socket: %s\n", strerror(errno));
                if ((returnValue < 0) && (errno != EINTR)) { return bytesSent; }
                size -= returnValue;
                buffer += returnValue;
                bytesSent += returnValue;
        }
        return bytesSent;
}


QueuePair * QueuePairFactory::acceptIncomingConnection(void *userData, uint32_t userDataSizeInBytes) {

        serializedQueuePair *receiveBuffer = (serializedQueuePair*) calloc(1, sizeof(serializedQueuePair));
        serializedQueuePair *sendBuffer = (serializedQueuePair*) calloc(1, sizeof(serializedQueuePair));

        int connectionSocket = accept(this->serverSocket, (sockaddr *) nullptr, nullptr);
        INFINITY_ASSERT(connectionSocket >= 0, "[INFINITY][QUEUES][FACTORY] Cannot open connection socket.\n");

        int32_t returnValue = readFromSocket(connectionSocket, reinterpret_cast<char*>(receiveBuffer), sizeof(serializedQueuePair));
        INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair), "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes received. Expected %lu. Received %d.\n",
                        sizeof(serializedQueuePair), returnValue);

	std::vector<char> userDataBuffer(receiveBuffer->userDataSize);
	returnValue = readFromSocket(connectionSocket, &userDataBuffer[0], receiveBuffer->userDataSize);
        INFINITY_ASSERT(returnValue == receiveBuffer->userDataSize, "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes received. Expected %lu. Received %d.\n",
                        sizeof(serializedQueuePair), returnValue);

        QueuePair *queuePair = new QueuePair(this->context);

        sendBuffer->localDeviceId = queuePair->getLocalDeviceId();
        sendBuffer->queuePairNumber = queuePair->getQueuePairNumber();
        sendBuffer->sequenceNumber = queuePair->getSequenceNumber();
        sendBuffer->userDataSize = userDataSizeInBytes;

        returnValue = sendToSocket(connectionSocket, reinterpret_cast<const char*>(sendBuffer), sizeof(serializedQueuePair));
        INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair),
                        "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes transmitted. Expected %lu. Received %d.\n", sizeof(serializedQueuePair), returnValue);

	returnValue = sendToSocket(connectionSocket, reinterpret_cast<char*>(userData), userDataSizeInBytes);
	INFINITY_ASSERT(returnValue == userDataSizeInBytes,
			"[INFINITY][QUEUES][FACTORY] Incorrect number of bytes transmitted. Expected %lu. Received %d.\n", sizeof(serializedQueuePair), returnValue);

        INFINITY_DEBUG("[INFINITY][QUEUES][FACTORY] Pairing (%u, %u, %u, %u)-(%u, %u, %u, %u)\n", queuePair->getLocalDeviceId(), queuePair->getQueuePairNumber(),
                        queuePair->getSequenceNumber(), userDataSizeInBytes, receiveBuffer->localDeviceId, receiveBuffer->queuePairNumber, receiveBuffer->sequenceNumber,
                        receiveBuffer->userDataSize);

        queuePair->activate(receiveBuffer->localDeviceId, receiveBuffer->queuePairNumber, receiveBuffer->sequenceNumber);
        queuePair->setRemoteUserData(userDataBuffer);

        this->context->registerQueuePair(queuePair);

        close(connectionSocket);
        free(receiveBuffer);
        free(sendBuffer);

        return queuePair;

}

QueuePair * QueuePairFactory::connectToRemoteHost(const char* hostAddress, uint16_t port, void *userData, uint32_t userDataSizeInBytes) {

        INFINITY_ASSERT(userDataSizeInBytes < infinity::core::Configuration::MAX_CONNECTION_USER_DATA_SIZE,
                        "[INFINITY][QUEUES][FACTORY] User data size is too large.\n")

        serializedQueuePair *receiveBuffer = (serializedQueuePair*) calloc(1, sizeof(serializedQueuePair));
        serializedQueuePair *sendBuffer = (serializedQueuePair*) calloc(1, sizeof(serializedQueuePair));

        sockaddr_in remoteAddress;
        memset(&(remoteAddress), 0, sizeof(sockaddr_in));
        remoteAddress.sin_family = AF_INET;
        inet_pton(AF_INET, hostAddress, &(remoteAddress.sin_addr));
        remoteAddress.sin_port = htons(port);

        int connectionSocket = socket(AF_INET, SOCK_STREAM, 0);
        INFINITY_ASSERT(connectionSocket >= 0, "[INFINITY][QUEUES][FACTORY] Cannot open connection socket.\n");

        int returnValue = connect(connectionSocket, (sockaddr *) &(remoteAddress), sizeof(sockaddr_in));
        INFINITY_ASSERT(returnValue == 0, "[INFINITY][QUEUES][FACTORY] Could not connect to server.\n");

        QueuePair *queuePair = new QueuePair(this->context);

        sendBuffer->localDeviceId = queuePair->getLocalDeviceId();
        sendBuffer->queuePairNumber = queuePair->getQueuePairNumber();
        sendBuffer->sequenceNumber = queuePair->getSequenceNumber();
        sendBuffer->userDataSize = userDataSizeInBytes;

        returnValue = sendToSocket(connectionSocket, reinterpret_cast<char*>(sendBuffer), sizeof(serializedQueuePair));
        INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair),
                        "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes transmitted. Expected %lu. Received %d.\n", sizeof(serializedQueuePair), returnValue);

	returnValue = sendToSocket(connectionSocket, reinterpret_cast<char*>(userData), userDataSizeInBytes);
        INFINITY_ASSERT(returnValue == userDataSizeInBytes,
                        "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes transmitted. Expected %lu. Received %d.\n", sizeof(serializedQueuePair), returnValue);
	

        returnValue = readFromSocket(connectionSocket, reinterpret_cast<char*>(receiveBuffer), sizeof(serializedQueuePair));
        INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair),
                        "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes received. Expected %lu. Received %d.\n", sizeof(serializedQueuePair), returnValue);

	std::vector<char> userDataBuffer(receiveBuffer->userDataSize);
	returnValue = readFromSocket(connectionSocket, &userDataBuffer[0], receiveBuffer->userDataSize);
        INFINITY_ASSERT(returnValue == receiveBuffer->userDataSize,
                        "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes received. Expected %lu. Received %d.\n", sizeof(serializedQueuePair), returnValue);


        INFINITY_DEBUG("[INFINITY][QUEUES][FACTORY] Pairing (%u, %u, %u, %u)-(%u, %u, %u, %u)\n", queuePair->getLocalDeviceId(), queuePair->getQueuePairNumber(),
                        queuePair->getSequenceNumber(), userDataSizeInBytes, receiveBuffer->localDeviceId, receiveBuffer->queuePairNumber, receiveBuffer->sequenceNumber,
                        receiveBuffer->userDataSize);

        queuePair->activate(receiveBuffer->localDeviceId, receiveBuffer->queuePairNumber, receiveBuffer->sequenceNumber);
        queuePair->setRemoteUserData(userDataBuffer);

        this->context->registerQueuePair(queuePair);

        close(connectionSocket);
        free(receiveBuffer);
        free(sendBuffer);

        return queuePair;

}

QueuePair* QueuePairFactory::createLoopback(const std::vector<char>& userData) {

        QueuePair *queuePair = new QueuePair(this->context);
        queuePair->activate(queuePair->getLocalDeviceId(), queuePair->getQueuePairNumber(), queuePair->getSequenceNumber());
        queuePair->setRemoteUserData(userData);

        this->context->registerQueuePair(queuePair);

        return queuePair;

}

} /* namespace queues */
} /* namespace infinity */
