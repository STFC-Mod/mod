#pragma once
#include "value_settings.h"

namespace mod_settings
{
using ReadResult  = ValueReadResult<bool>;
using Definition  = ValueDefinition<bool>;
using Snapshot    = ValueSnapshot<bool>;
using WriteResult = ValueWriteResult<bool>;
class BooleanSetting : public ValueSetting<bool>
{
public:
  using ValueSetting<bool>::ValueSetting;
};

enum class Registration { Added, Duplicate, Invalid, Frozen };
} // namespace mod_settings
