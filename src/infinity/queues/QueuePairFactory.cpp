/**
 * Queues - Queue Pair Factory
 *
 * (c) 2018 Claude Barthels, ETH Zurich
 * Contact: claudeb@inf.ethz.ch
 *
 */

#include "QueuePairFactory.h"

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>

#include <infinity/core/Configuration.h>
#include <infinity/utils/Address.h>
#include <infinity/utils/Debug.h>

namespace infinity {
namespace queues {

typedef struct {

  uint16_t localDeviceId = 0;
  uint32_t queuePairNumber = 0;
  uint32_t sequenceNumber = 0;
  uint32_t userDataSize = 0;

} serializedQueuePair;

QueuePairFactory::QueuePairFactory(
    const std::shared_ptr<infinity::core::Context> &context)
    : context(context) {}

QueuePairFactory::~QueuePairFactory() {

  if (serverSocket >= 0) {
    close(serverSocket);
  }
}

void QueuePairFactory::bindToPort(uint16_t port) {

  serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  INFINITY_ASSERT(serverSocket >= 0,
                  "[INFINITY][QUEUES][FACTORY] Cannot open server socket.\n");

  sockaddr_in serverAddress;
  memset(&(serverAddress), 0, sizeof(sockaddr_in));
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(port);

  int32_t enabled = 1;
  int32_t returnValue = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
                                   &enabled, sizeof(enabled));
  INFINITY_ASSERT(returnValue == 0, "[INFINITY][QUEUES][FACTORY] Cannot set "
                                    "socket option to reuse address.\n");

  returnValue =
      bind(serverSocket, (sockaddr *)&serverAddress, sizeof(sockaddr_in));
  INFINITY_ASSERT(
      returnValue == 0,
      "[INFINITY][QUEUES][FACTORY] Cannot bind to local address and port.\n");

  returnValue = listen(serverSocket, 128);
  INFINITY_ASSERT(
      returnValue == 0,
      "[INFINITY][QUEUES][FACTORY] Cannot listen on server socket.\n");

  std::string ipAddressOfDevice =
      infinity::utils::Address::getIpAddressOfInterface(
          infinity::core::Configuration::DEFAULT_IB_DEVICE);
  INFINITY_DEBUG("[INFINITY][QUEUES][FACTORY] Accepting connections on IP "
                 "address %s and port %d.\n",
                 ipAddressOfDevice.c_str(), port);
}

/* Returns the port we have bound to; we don't just store the port we
 passed into bindToPort so we can pass in a port of 0 and get an
 ephemeral port.
*/
uint16_t QueuePairFactory::getPort() {
  if (serverSocket) {
    struct sockaddr_in my_address;
    memset(&my_address, 0, sizeof(my_address));
    socklen_t length = sizeof(my_address);
    if (getsockname(serverSocket, reinterpret_cast<sockaddr *>(&my_address),
                    &length) == 0) {
      return ntohs(my_address.sin_port);
    }
  }
  return 0;
}

int32_t QueuePairFactory::getSocket() const
{
    return serverSocket;
}

int32_t QueuePairFactory::readFromSocket(int32_t socket, char *buffer,
                                         uint32_t size) {
  int32_t bytesReceived = 0;
  while (size) {
    int32_t returnValue = recv(socket, buffer, size, 0);
    INFINITY_ASSERT(
        returnValue >= 0,
        "[INFINITY][QUEUES][FACTORY] Problem reading from socket: %s\n",
        strerror(errno));
    if ((returnValue <= 0) && (errno != EINTR)) {
      return bytesReceived;
    }
    size -= returnValue;
    buffer += returnValue;
    bytesReceived += returnValue;
  }
  return bytesReceived;
}

int32_t QueuePairFactory::sendToSocket(int32_t socket, const char *buffer,
                                       uint32_t size) {
  int32_t bytesSent = 0;
  while (size) {
    int32_t returnValue = send(socket, buffer, size, 0);
    INFINITY_ASSERT(
        returnValue >= 0,
        "[INFINITY][QUEUES][FACTORY] Problem sending on socket: %s\n",
        strerror(errno));
    if ((returnValue < 0) && (errno != EINTR)) {
      return bytesSent;
    }
    size -= returnValue;
    buffer += returnValue;
    bytesSent += returnValue;
  }
  return bytesSent;
}

std::shared_ptr<QueuePair>
QueuePairFactory::acceptIncomingConnection(void *userData,
                                           uint32_t userDataSizeInBytes) {

  serializedQueuePair receiveBuffer;
  serializedQueuePair sendBuffer;

  int connectionSocket =
      accept(this->serverSocket, (sockaddr *)nullptr, nullptr);
  INFINITY_ASSERT(
      connectionSocket >= 0,
      "[INFINITY][QUEUES][FACTORY] Cannot open connection socket.\n");

  int32_t returnValue =
      readFromSocket(connectionSocket, reinterpret_cast<char *>(&receiveBuffer),
                     sizeof(serializedQueuePair));
  INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "received. Expected %lu. Received %d.\n",
                  sizeof(serializedQueuePair), returnValue);

  std::vector<char> userDataBuffer(receiveBuffer.userDataSize);
  returnValue = readFromSocket(connectionSocket, &userDataBuffer[0],
                               receiveBuffer.userDataSize);
  INFINITY_ASSERT(returnValue == int32_t(receiveBuffer.userDataSize),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "received. Expected %lu. Received %d.\n",
                  sizeof(serializedQueuePair), returnValue);

  auto queuePair = std::make_shared<QueuePair>(this->context);

  sendBuffer.localDeviceId = queuePair->getLocalDeviceId();
  sendBuffer.queuePairNumber = queuePair->getQueuePairNumber();
  sendBuffer.sequenceNumber = queuePair->getSequenceNumber();
  sendBuffer.userDataSize = userDataSizeInBytes;

  returnValue = sendToSocket(connectionSocket,
                             reinterpret_cast<const char *>(&sendBuffer),
                             sizeof(serializedQueuePair));
  INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "transmitted. Expected %lu. Received %d.\n",
                  sizeof(serializedQueuePair), returnValue);

  returnValue =
      sendToSocket(connectionSocket, reinterpret_cast<char *>(userData),
                   userDataSizeInBytes);
  INFINITY_ASSERT(returnValue == int32_t(userDataSizeInBytes),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "transmitted. Expected %u. Received %d.\n",
                  userDataSizeInBytes, returnValue);

  INFINITY_DEBUG(
      "[INFINITY][QUEUES][FACTORY] Pairing (%u, %u, %u, %u)-(%u, %u, %u, %u)\n",
      queuePair->getLocalDeviceId(), queuePair->getQueuePairNumber(),
      queuePair->getSequenceNumber(), userDataSizeInBytes,
      receiveBuffer.localDeviceId, receiveBuffer.queuePairNumber,
      receiveBuffer.sequenceNumber, receiveBuffer.userDataSize);

  queuePair->activate(receiveBuffer.localDeviceId,
                      receiveBuffer.queuePairNumber,
                      receiveBuffer.sequenceNumber);
  queuePair->setRemoteUserData(userDataBuffer);

  this->context->registerQueuePair(queuePair);

  close(connectionSocket);

  return queuePair;
}

std::shared_ptr<QueuePair>
QueuePairFactory::connectToRemoteHost(const char *hostAddress, uint16_t port,
                                      void *userData,
                                      uint32_t userDataSizeInBytes) {

  INFINITY_ASSERT(
      userDataSizeInBytes <
          infinity::core::Configuration::MAX_CONNECTION_USER_DATA_SIZE,
      "[INFINITY][QUEUES][FACTORY] User data size is too large.\n")

  serializedQueuePair receiveBuffer;
  serializedQueuePair sendBuffer;

  sockaddr_in remoteAddress;
  memset(&(remoteAddress), 0, sizeof(sockaddr_in));
  remoteAddress.sin_family = AF_INET;
  struct hostent *hostEntry = gethostbyname(hostAddress);
  INFINITY_ASSERT(
      hostEntry != nullptr,
      "[INFINITY][QUEUES][FACTORY] Unable to get IP address for %s: %s.\n",
      hostAddress, hstrerror(h_errno));
  memcpy(&remoteAddress.sin_addr, hostEntry->h_addr_list[0],
         hostEntry->h_length);

  remoteAddress.sin_port = htons(port);

  int connectionSocket = socket(AF_INET, SOCK_STREAM, 0);
  INFINITY_ASSERT(
      connectionSocket >= 0,
      "[INFINITY][QUEUES][FACTORY] Cannot open connection socket.\n");

  int32_t returnValue = connect(connectionSocket, (sockaddr *)&(remoteAddress),
                                sizeof(sockaddr_in));
  INFINITY_ASSERT(returnValue == 0,
                  "[INFINITY][QUEUES][FACTORY] Could not connect to server.\n");

  auto queuePair = std::make_shared<QueuePair>(this->context);

  sendBuffer.localDeviceId = queuePair->getLocalDeviceId();
  sendBuffer.queuePairNumber = queuePair->getQueuePairNumber();
  sendBuffer.sequenceNumber = queuePair->getSequenceNumber();
  sendBuffer.userDataSize = userDataSizeInBytes;

  returnValue =
      sendToSocket(connectionSocket, reinterpret_cast<char *>(&sendBuffer),
                   sizeof(serializedQueuePair));
  INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "transmitted. Expected %lu. Received %d.\n",
                  sizeof(serializedQueuePair), returnValue);

  returnValue =
      sendToSocket(connectionSocket, reinterpret_cast<char *>(userData),
                   userDataSizeInBytes);
  INFINITY_ASSERT(returnValue == int32_t(userDataSizeInBytes),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "transmitted. Expected %u. Received %d.\n",
                  userDataSizeInBytes, returnValue);

  returnValue =
      readFromSocket(connectionSocket, reinterpret_cast<char *>(&receiveBuffer),
                     sizeof(serializedQueuePair));
  INFINITY_ASSERT(returnValue == sizeof(serializedQueuePair),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "received. Expected %lu. Received %d.\n",
                  sizeof(serializedQueuePair), returnValue);

  std::vector<char> userDataBuffer(receiveBuffer.userDataSize);
  returnValue = readFromSocket(connectionSocket, &userDataBuffer[0],
                               receiveBuffer.userDataSize);
  INFINITY_ASSERT(returnValue == int(receiveBuffer.userDataSize),
                  "[INFINITY][QUEUES][FACTORY] Incorrect number of bytes "
                  "received. Expected %u. Received %d.\n",
                  receiveBuffer.userDataSize, returnValue);

  INFINITY_DEBUG(
      "[INFINITY][QUEUES][FACTORY] Pairing (%u, %u, %u, %u)-(%u, %u, %u, %u)\n",
      queuePair->getLocalDeviceId(), queuePair->getQueuePairNumber(),
      queuePair->getSequenceNumber(), userDataSizeInBytes,
      receiveBuffer.localDeviceId, receiveBuffer.queuePairNumber,
      receiveBuffer.sequenceNumber, receiveBuffer.userDataSize);

  queuePair->activate(receiveBuffer.localDeviceId,
                      receiveBuffer.queuePairNumber,
                      receiveBuffer.sequenceNumber);
  queuePair->setRemoteUserData(userDataBuffer);

  this->context->registerQueuePair(queuePair);

  close(connectionSocket);

  return queuePair;
}

std::shared_ptr<QueuePair>
QueuePairFactory::createLoopback(const std::vector<char> &userData) {

  auto queuePair = std::make_shared<QueuePair>(this->context);
  queuePair->activate(queuePair->getLocalDeviceId(),
                      queuePair->getQueuePairNumber(),
                      queuePair->getSequenceNumber());
  queuePair->setRemoteUserData(userData);

  this->context->registerQueuePair(queuePair);

  return queuePair;
}

} /* namespace queues */
} /* namespace infinity */
