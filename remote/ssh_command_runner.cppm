module;

#include <string>

export module vaultium_remote_ssh_command_runner;

import vaultium_core_inventory;
import vaultium_remote_ssh_client;

export namespace vaultium::remote {

/**
 * @brief Adapts an SshClient to the core ICommandRunner interface so the
 * InventoryService can discover a remote machine over SSH.
 */
class SshCommandRunner final : public vaultium::ICommandRunner {
public:
    explicit SshCommandRunner(SshClient& client);
    [[nodiscard]] auto run(const std::string& command) -> vaultium::CommandResult override;

private:
    SshClient& m_client;
};

} // namespace vaultium::remote
