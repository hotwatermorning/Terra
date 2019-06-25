#pragma once

#include <vector>

#include "../project/Sequence.hpp"

NS_HWM_BEGIN

std::vector<SequencePtr> CreateSequenceFromSMF(String path);

NS_HWM_END
