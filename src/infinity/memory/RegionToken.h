/*
 * Memory - Region Token
 *
 * (c) 2018 Claude Barthels, ETH Zurich
 * Contact: claudeb@inf.ethz.ch
 *
 */

#ifndef MEMORY_REGIONTOKEN_H_
#define MEMORY_REGIONTOKEN_H_

#include <iostream>
#include <stdint.h>
#include <infinity/memory/RegionType.h>
#include <infinity/memory/Region.h>

namespace infinity {
namespace memory {

class RegionToken {

public:
  RegionToken();
  RegionToken(Region *memoryRegion, RegionType memoryRegionType,
              uint64_t sizeInBytes, uint64_t address, uint32_t localKey,
              uint32_t remoteKey);

public:
  Region *getMemoryRegion() const;
  RegionType getMemoryRegionType() const;
  uint64_t getSizeInBytes() const;
  uint64_t getRemainingSizeInBytes(uint64_t offset) const;
  uint64_t getAddress() const;
  uint64_t getAddressWithOffset(uint64_t offset) const;
  uint32_t getLocalKey() const;
  uint32_t getRemoteKey() const;

private:
  Region *memoryRegion = nullptr;
  RegionType memoryRegionType = UNKNOWN;
  uint64_t sizeInBytes = 0;
  uint64_t address = 0;
  uint32_t localKey = 0;
  uint32_t remoteKey = 0;
};

std::ostream &operator<<(std::ostream &os, const RegionToken &regionToken);
} /* namespace memory */
} /* namespace infinity */

#endif /* MEMORY_REGIONTOKEN_H_ */
