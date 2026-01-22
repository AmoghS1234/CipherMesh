#include "vault_service.hpp"
#include "../../src/core/vault.hpp"
#include "../../src/core/crypto.hpp"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace CipherMesh::Core;

VaultService::VaultService() : m_vault(nullptr) {
}

VaultService::~VaultService() {
    if (m_vault) {
        m_vault->lock();
    }
}

std::string VaultService::getDefaultVaultPath() {
    const char* home = getenv("HOME");
    if (!home) {
        home = "/tmp";
    }
    
    std::string standardPath = "/home/amogh/.local/share/CipherMesh-Desktop/ciphermesh.db";
    std::ifstream standardFile(standardPath);
    if (standardFile.good()) {
        return standardPath;
    }

    std::string fullPath = std::string(home) + "/Projects/CipherMesh/build/ciphermesh.db";
    std::ifstream fullFile(fullPath);
    if (fullFile.good()) {
        return fullPath;
    }

    std::string buildPath = "build/ciphermesh.db";
    std::ifstream buildFile(buildPath);
    if (buildFile.good()) {
        return buildPath;
    }

    std::string parentBuildPath = "../build/ciphermesh.db";
    std::ifstream parentBuildFile(parentBuildPath);
    if (parentBuildFile.good()) {
        return parentBuildPath;
    }

    std::string currentDirPath = "ciphermesh.db";
    std::ifstream currentDirFile(currentDirPath);
    if (currentDirFile.good()) {
        return currentDirPath;
    }

    return standardPath;
}

json VaultService::handleRequest(const json& request) {
    json response;
    
    try {
        std::string action = request.value("action", "");
        if (action.empty()) {
            action = request.value("type", "");
        }
        
        if (action == "VERIFY_MASTER_PASSWORD") {
            return handleVerifyMasterPassword(request);
        } else if (action == "GET_CREDENTIALS") {
            return handleGetCredentials(request);
        } else if (action == "GET_CREDENTIAL_BY_ID") {
            return handleGetCredentialById(request);
        } else if (action == "SAVE_CREDENTIALS") {
            return handleSaveCredentials(request);
        } else if (action == "LIST_GROUPS") {
            return handleListGroups(request);
        } else if (action == "PING") {
            response["status"] = "success";
            response["message"] = "pong";
            return response;
        } else {
            response["status"] = "error";
            response["error"] = "Unknown action: " + action;
            return response;
        }
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["error"] = std::string("Exception: ") + e.what();
        return response;
    }
}

json VaultService::handleVerifyMasterPassword(const json& request) {
    json response;
    
    try {
        std::string masterPassword = request.value("masterPassword", "");
        if (masterPassword.empty()) {
            masterPassword = request.value("password", "");
        }
        
        if (masterPassword.empty()) {
            response["status"] = "error";
            response["error"] = "Password is required";
            std::cerr << "[Vault Service] Error: Password is required" << std::endl;
            return response;
        }
        
        std::string vaultPath = request.value("vaultPath", getDefaultVaultPath());
        std::cerr << "[Vault Service] Using vault path: " << vaultPath << std::endl;
        
        if (vaultPath.empty()) {
            response["status"] = "error";
            response["error"] = "Could not determine vault path";
            std::cerr << "[Vault Service] Error: Could not determine vault path" << std::endl;
            return response;
        }

        std::ifstream vaultFile(vaultPath);
        if (!vaultFile.good()) {
            response["status"] = "error";
            response["error"] = "Vault file not found at: " + vaultPath;
            std::cerr << "[Vault Service] Error: Vault file not found at: " << vaultPath << std::endl;
            return response;
        }
        
        std::cerr << "[Vault Service] Vault file found, attempting to load..." << std::endl;

        std::unique_ptr<Vault> vault = std::make_unique<Vault>();

        if (!vault->loadVault(vaultPath, masterPassword)) {
            response["status"] = "error";
            response["error"] = "Incorrect master password";
            std::cerr << "[Vault Service] Error: Incorrect master password" << std::endl;
            return response;
        }

        m_vault = std::move(vault);
        
        response["status"] = "success";
        response["message"] = "Master password verified";
        response["verified"] = true;
        std::cerr << "[Vault Service] Password verified successfully" << std::endl;
        return response;
        
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["error"] = std::string("Error verifying password: ") + e.what();
        std::cerr << "[Vault Service] Exception: " << e.what() << std::endl;
        return response;
    }
}

json VaultService::handleGetCredentials(const json& request) {
    json response;
    
    try {
        if (!m_vault || m_vault->isLocked()) {
            response["status"] = "error";
            response["error"] = "Vault is locked. Please verify master password first.";
            return response;
        }
        
        std::string url = request["url"];
        std::string username = request.value("username", "");

        std::vector<VaultEntry> entries = m_vault->findEntriesByLocation(url);
        
        if (entries.empty()) {
            response["status"] = "error";
            response["error"] = "No credentials found for: " + url;
            return response;
        }

        if (!username.empty()) {
            auto it = std::find_if(entries.begin(), entries.end(),
                [&username](const VaultEntry& e) { return e.username == username; });
            
            if (it == entries.end()) {
                response["status"] = "error";
                response["error"] = "No credentials found for username: " + username;
                return response;
            }

            std::string password = m_vault->getDecryptedPassword(it->id);
            
            response["status"] = "success";
            response["username"] = it->username;
            response["password"] = password;
            response["title"] = it->title;
            return response;
        }

        json credentials = json::array();
        for (const auto& entry : entries) {
            std::string decryptedPassword;
            try {
                decryptedPassword = m_vault->getDecryptedPassword(entry.id);
            } catch (const std::exception& e) {
                std::cerr << "[Vault Service] Failed to decrypt password for entry " 
                          << entry.id << ": " << e.what() << std::endl;
                continue;
            }
            
            json cred;
            cred["id"] = entry.id;
            cred["username"] = entry.username;
            cred["password"] = decryptedPassword;
            cred["title"] = entry.title;
            credentials.push_back(cred);
        }
        
        response["status"] = "multiple";
        response["credentials"] = credentials;
        return response;
        
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["error"] = std::string("Error getting credentials: ") + e.what();
        return response;
    }
}

json VaultService::handleGetCredentialById(const json& request) {
    json response;
    
    try {
        if (!m_vault || m_vault->isLocked()) {
            response["status"] = "error";
            response["error"] = "Vault is locked";
            return response;
        }
        
        int entryId = request["entryId"];
        
        std::string password = m_vault->getDecryptedPassword(entryId);

        std::vector<VaultEntry> allEntries = m_vault->getEntries();
        auto it = std::find_if(allEntries.begin(), allEntries.end(),
            [entryId](const VaultEntry& e) { return e.id == entryId; });
        
        if (it == allEntries.end()) {
            response["status"] = "error";
            response["error"] = "Entry not found";
            return response;
        }
        
        response["status"] = "success";
        response["username"] = it->username;
        response["password"] = password;
        response["title"] = it->title;
        return response;
        
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["error"] = std::string("Error: ") + e.what();
        return response;
    }
}

json VaultService::handleSaveCredentials(const json& request) {
    json response;
    
    try {
        if (!m_vault || m_vault->isLocked()) {
            response["status"] = "error";
            response["error"] = "Vault is locked. Please verify master password first.";
            return response;
        }
        
        std::string url = request["url"];
        std::string username = request["username"];
        std::string password = request["password"];
        std::string groupName = request.value("group", "Default");
        std::string title = request.value("title", url);

        if (m_vault->entryExists(username, url)) {
            response["status"] = "error";
            response["error"] = "Entry already exists for this username and URL";
            return response;
        }

        if (!m_vault->setActiveGroup(groupName)) {
            if (!m_vault->addGroup(groupName)) {
                response["status"] = "error";
                response["error"] = "Failed to create group: " + groupName;
                return response;
            }
            if (!m_vault->setActiveGroup(groupName)) {
                response["status"] = "error";
                response["error"] = "Failed to set active group: " + groupName;
                return response;
            }
        }

        VaultEntry entry;
        entry.title = title;
        entry.username = username;
        entry.notes = "Saved from browser extension";
        entry.url = url;
        entry.locations.push_back(Location(-1, "URL", url));
        
        if (!m_vault->addEntry(entry, password)) {
            response["status"] = "error";
            response["error"] = "Failed to save credentials";
            return response;
        }
        
        response["status"] = "success";
        response["message"] = "Credentials saved successfully";
        return response;
        
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["error"] = std::string("Error saving credentials: ") + e.what();
        return response;
    }
}

json VaultService::handleListGroups(const json& request) {
    json response;
    
    try {
        if (!m_vault || m_vault->isLocked()) {
            response["status"] = "error";
            response["error"] = "Vault is locked. Please verify master password first.";
            return response;
        }
        
        std::vector<std::string> groupNames = m_vault->getGroupNames();
        
        json groups = json::array();
        for (const auto& name : groupNames) {
            groups.push_back(name);
        }
        
        response["status"] = "success";
        response["groups"] = groups;
        return response;
        
    } catch (const std::exception& e) {
        response["status"] = "error";
        response["error"] = std::string("Error listing groups: ") + e.what();
        return response;
    }
}
