module;

#include <string>
#include <utility>

module vaultium_remote_ssh_command_runner;

namespace vaultium::remote {

SshCommandRunner::SshCommandRunner(SshClient& client)
    : m_client { client }
{
}

auto SshCommandRunner::run(const std::string& command) -> vaultium::CommandResult
{
    const auto result = m_client.execute(command);
    return vaultium::CommandResult { result.exitCode, result.output };
}

} // namespace vaultium::remote
