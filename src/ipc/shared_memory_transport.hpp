#pragma once

// Compatibility include for existing internal callers. The Linux shared-memory
// backend itself lives under src/ipc/transport/ and uses anonymous memfd
// mappings transferred over a Unix-domain control socket.
#include "transport/memfd_shared_memory_transport.hpp"
