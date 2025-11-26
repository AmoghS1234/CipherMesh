#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace CipherMesh {
namespace Core {
    class Vault;
}
}

using json = nlohmann::json;

class VaultService {
public:
    VaultService();
    ~VaultService();
    
    // Main request handler
    json handleRequest(const json& request);
    
private:
    std::unique_ptr<CipherMesh::Core::Vault> m_vault;
    
    std::string getDefaultVaultPath();
    
    // Request handlers
    json handleVerifyMasterPassword(const json& request);
    json handleGetCredentials(const json& request);
    json handleGetCredentialById(const json& request);
    json handleSaveCredentials(const json& request);
    json handleListGroups(const json& request);
};
