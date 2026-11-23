#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// One operator referenced by a particle system. common is true only for the
// small function-name list this reader recognizes. It is not a simulation.
struct PcfOperatorUse {
  std::string systemName;
  std::string functionName;
  bool common = false;
};

// Binary DMX version 2 / pcf, the layout used by TF2's misc VPK. Other
// encodings return valid=false. A corrupt file returns an error and does not
// throw. Operators are capped; counts include everything that was walked.
struct PcfOperatorSummary {
  std::string encoding;
  int encodingVersion = 0;
  std::string format;
  int formatVersion = 0;
  std::size_t stringCount = 0;
  std::size_t elementCount = 0;
  std::size_t systemCount = 0;
  std::size_t operatorCount = 0;
  std::size_t commonOperatorCount = 0;
  std::vector<PcfOperatorUse> operators;
  std::string error;
  bool valid = false;
};

bool pcfFunctionNameIsCommon(const std::string& functionName);

bool readPcfOperators(const std::uint8_t* bytes, std::size_t size, PcfOperatorSummary& out);

} // namespace tf2::native
