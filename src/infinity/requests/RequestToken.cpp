/**
 * Requests - Request Token
 *
 * (c) 2018 Claude Barthels, ETH Zurich
 * Contact: claudeb@inf.ethz.ch
 *
 */

#include "RequestToken.h"

namespace infinity {
namespace requests {

RequestToken::RequestToken(std::shared_ptr<infinity::core::Context> context)
    : context(context) {
  this->status.store(-1);
  this->completed.store(false);
  this->region = nullptr;
  this->userData = nullptr;
  this->userDataValid = false;
  this->userDataSize = 0;
  this->immediateValue = 0;
  this->immediateValueValid = false;
}

void RequestToken::setStatus(ibv_wc_status status) {
  this->status.store(status);
  this->completed.store(true);
}

ibv_wc_status RequestToken::getStatus() const {
  return ibv_wc_status(this->status.load());
}

const char *RequestToken::getStatusString() const {
  if (this->status == -1) {
    return "Status not set";
  }
  return ibv_wc_status_str(this->getStatus());
}

bool RequestToken::checkIfCompleted() {
  if (this->completed.load()) {
    return true;
  } else {
    this->context->pollSendCompletionQueue();
    return this->completed.load();
  }
}

void RequestToken::waitUntilCompleted() {
  while (!this->completed.load()) {
    this->context->pollSendCompletionQueue();
  }
}

bool RequestToken::wasSuccessful() {
  return this->status.load() == IBV_WC_SUCCESS;
}

void RequestToken::reset() {
  this->status.store(-1);
  this->completed.store(false);
  this->region = nullptr;
  this->userData = nullptr;
  this->userDataValid = false;
  this->userDataSize = 0;
  this->immediateValue = 0;
  this->immediateValueValid = false;
}

void RequestToken::setRegion(std::shared_ptr<infinity::memory::Region> region) {
  this->region = region;
}

std::shared_ptr<infinity::memory::Region> RequestToken::getRegion() {
  return this->region;
}

void RequestToken::setUserData(void *userData, uint32_t userDataSize) {
  this->userData = userData;
  this->userDataSize = userDataSize;
  this->userDataValid = true;
}

void *RequestToken::getUserData() { return this->userData; }

bool RequestToken::hasUserData() { return this->userDataValid; }

uint32_t RequestToken::getUserDataSize() { return this->userDataSize; }

void RequestToken::setImmediateValue(uint32_t immediateValue) {
  this->immediateValue = immediateValue;
  this->immediateValueValid = true;
}

uint32_t RequestToken::getImmediateValue() { return this->immediateValue; }

bool RequestToken::hasImmediateValue() { return this->immediateValueValid; }

} /* namespace requests */
} /* namespace infinity */
