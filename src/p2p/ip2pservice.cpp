#include "ip2pservice.hpp"

namespace CipherMesh {
namespace P2P {

// This defines the virtual destructor for the interface.
// It allows classes to inherit from IP2PService safely.
IP2PService::~IP2PService() = default;

} // namespace P2P
} // namespace CipherMesh