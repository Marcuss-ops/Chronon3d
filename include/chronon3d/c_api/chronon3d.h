#pragma once

/*
 * Compatibility path retained for existing Chronon3D consumers.  The public
 * contract is owned by <chronon/abi.h>; this file must contain no declarations
 * of its own so the two repositories cannot drift.
 */
#include <chronon/abi.h>

#ifndef CHRONON3D_API
#define CHRONON3D_API CHRONON_API
#endif
