#include "Context.h"
#include "Configuration.h"

// Must be less than MAX_CQE
const uint32_t infinity::core::Configuration::sendCompletionQueueLength(Context * context)
{
  ibv_device_attr deviceAttributes;
  context->getDeviceAttr(&deviceAttributes);
  return deviceAttributes.max_qp_wr * .25;
}


// Must be less than MAX_CQE
const uint32_t infinity::core::Configuration::recvCompletionQueueLength(Context * context)
{
  ibv_device_attr deviceAttributes;
  context->getDeviceAttr(&deviceAttributes);
  return deviceAttributes.max_qp_wr * .25;
}

// Must be less than MAX_SRQ_WR
const uint32_t infinity::core::Configuration::sharedRecvQueueLength(Context * context)
{
  ibv_device_attr deviceAttributes;
  context->getDeviceAttr(&deviceAttributes);
  return deviceAttributes.max_srq_wr - 1;
}

// Must be less than (MAX_QP_WR * MAX_QP)
const uint32_t infinity::core::Configuration::maxNumberOfOutstandingRequests(Context * context)
{
  ibv_device_attr deviceAttributes;
  context->getDeviceAttr(&deviceAttributes);
  return deviceAttributes.max_qp_wr;
}


const uint32_t infinity::core::Configuration::maxNumberOfSGEElements(Context * context)
{
  ibv_device_attr deviceAttributes;
  context->getDeviceAttr(&deviceAttributes);
  return deviceAttributes.max_sge * .125;
}