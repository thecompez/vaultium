module;

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

module vaultium_remote_ssh_client;

import vaultium_core_logger;

namespace vaultium::remote {
namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

class LibSsh2Global final {
public:
    LibSsh2Global()
    {
        if (libssh2_init(0) != 0) throw std::runtime_error("libssh2 initialization failed.");
    }
    ~LibSsh2Global() { libssh2_exit(); }
};

auto ensureLibSsh2Global() -> void
{
    static LibSsh2Global global;
}

[[nodiscard]] auto sessionFromVoid(void* session) -> LIBSSH2_SESSION*
{
    return static_cast<LIBSSH2_SESSION*>(session);
}

[[nodiscard]] auto lastSessionError(LIBSSH2_SESSION* session) -> std::string
{
    char* message {};
    const auto length = libssh2_session_last_error(session, &message, nullptr, 0);
    if (length <= 0 || message == nullptr) return "Unknown libssh2 error.";
    return std::string { message, static_cast<std::size_t>(length) };
}

[[nodiscard]] auto makeDeadline(std::chrono::seconds timeout) -> Deadline
{
    if (timeout.count() <= 0) throw std::runtime_error("SSH timeout must be greater than zero.");
    return Clock::now() + timeout;
}

[[nodiscard]] auto remainingMilliseconds(Deadline deadline) -> int
{
    const auto now = Clock::now();
    if (now >= deadline) return 0;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<int>(std::min<long long>(remaining, std::numeric_limits<int>::max()));
}

auto waitSocket(int socketFd, LIBSSH2_SESSION* session, Deadline deadline, const char* operation) -> void
{
    pollfd descriptor {};
    descriptor.fd = socketFd;

    const auto direction = libssh2_session_block_directions(session);
    if ((direction & LIBSSH2_SESSION_BLOCK_INBOUND) != 0) descriptor.events |= POLLIN;
    if ((direction & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0) descriptor.events |= POLLOUT;
    if (descriptor.events == 0) descriptor.events = POLLIN | POLLOUT;

    while (true) {
        const auto timeout = remainingMilliseconds(deadline);
        if (timeout <= 0) throw std::runtime_error(std::string { operation } + " timed out.");
        const auto result = ::poll(&descriptor, 1, timeout);
        if (result > 0) return;
        if (result == 0) throw std::runtime_error(std::string { operation } + " timed out.");
        if (errno == EINTR) continue;
        throw std::runtime_error(std::string { operation } + " socket polling failed: " + std::strerror(errno));
    }
}

auto waitWhileAgain(
    int socketFd,
    LIBSSH2_SESSION* session,
    Deadline deadline,
    const char* operationName,
    auto&& operation
) -> int
{
    int result {};
    while ((result = operation()) == LIBSSH2_ERROR_EAGAIN) {
        waitSocket(socketFd, session, deadline, operationName);
    }
    return result;
}

[[nodiscard]] auto openTcpSocket(
    const std::string& host,
    std::uint16_t port,
    std::chrono::seconds timeout
) -> int
{
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses {};
    const auto portString = std::to_string(port);
    const auto status = ::getaddrinfo(host.c_str(), portString.c_str(), &hints, &addresses);
    if (status != 0) throw std::runtime_error("getaddrinfo failed: " + std::string { gai_strerror(status) });

    const auto deadline = makeDeadline(timeout);
    int connectedSocket = -1;

    for (auto* address = addresses; address != nullptr && Clock::now() < deadline; address = address->ai_next) {
        const auto socketFd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socketFd == -1) continue;

        const auto flags = ::fcntl(socketFd, F_GETFL, 0);
        if (flags == -1 || ::fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) == -1) {
            ::close(socketFd);
            continue;
        }

        const auto result = ::connect(socketFd, address->ai_addr, address->ai_addrlen);
        if (result == 0) {
            connectedSocket = socketFd;
            break;
        }

        if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            pollfd descriptor { socketFd, POLLOUT, 0 };
            while (true) {
                const auto milliseconds = remainingMilliseconds(deadline);
                if (milliseconds <= 0) break;
                const auto pollResult = ::poll(&descriptor, 1, milliseconds);
                if (pollResult > 0) {
                    int socketError {};
                    socklen_t length = sizeof(socketError);
                    if (::getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 && socketError == 0) {
                        connectedSocket = socketFd;
                    }
                    break;
                }
                if (pollResult == 0) break;
                if (errno == EINTR) continue;
                break;
            }
        }

        if (connectedSocket == socketFd) break;
        ::close(socketFd);
    }

    ::freeaddrinfo(addresses);
    if (connectedSocket == -1) {
        throw std::runtime_error("Connection to " + host + ":" + std::to_string(port) + " timed out or failed.");
    }
    return connectedSocket;
}

[[nodiscard]] auto queryAuthMethods(
    int socketFd,
    LIBSSH2_SESSION* session,
    const std::string& username,
    Deadline deadline
) -> std::string
{
    while (true) {
        char* methods = libssh2_userauth_list(session, username.c_str(), static_cast<unsigned int>(username.size()));
        if (methods != nullptr) return std::string { methods };
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) return {};
        waitSocket(socketFd, session, deadline, "SSH authentication-method query");
    }
}

auto writeFilePermissionsOwnerOnly(const std::filesystem::path& path) -> void
{
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
}

extern "C" void keyboardInteractiveCallback(
    const char*, int,
    const char*, int,
    int numPrompts,
    const LIBSSH2_USERAUTH_KBDINT_PROMPT*,
    LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
    void** abstract)
{
    const auto* password = static_cast<const std::string*>(*abstract);
    for (int index = 0; index < numPrompts; ++index) {
        if (password == nullptr) continue;
        responses[index].text = static_cast<char*>(std::malloc(password->size() + 1));
        if (responses[index].text != nullptr) {
            std::memcpy(responses[index].text, password->c_str(), password->size() + 1);
            responses[index].length = static_cast<unsigned int>(password->size());
        }
    }
}

class SftpSession final {
public:
    SftpSession(LIBSSH2_SESSION* session, int socketFd, Deadline deadline)
    {
        while ((m_sftp = libssh2_sftp_init(session)) == nullptr) {
            if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
                throw std::runtime_error("Could not initialize SFTP session: " + lastSessionError(session));
            }
            waitSocket(socketFd, session, deadline, "SFTP initialization");
        }
    }
    SftpSession(const SftpSession&) = delete;
    auto operator=(const SftpSession&) -> SftpSession& = delete;
    ~SftpSession()
    {
        if (m_sftp != nullptr) libssh2_sftp_shutdown(m_sftp);
    }
    [[nodiscard]] auto get() const -> LIBSSH2_SFTP* { return m_sftp; }
private:
    LIBSSH2_SFTP* m_sftp {};
};

} // namespace

SshClient::SshClient(SshConnectionConfig config)
    : m_config { std::move(config) }
{
}

SshClient::~SshClient()
{
    closeSession();
}

auto SshClient::connect() -> void
{
    ensureLibSsh2Global();
    closeSession();

    const auto deadline = makeDeadline(m_config.connectTimeout);
    m_socket = openTcpSocket(m_config.host, m_config.port, m_config.connectTimeout);

    auto* session = libssh2_session_init();
    if (session == nullptr) {
        ::close(m_socket);
        m_socket = -1;
        throw std::runtime_error("Could not initialize SSH session.");
    }
    m_session = session;
    libssh2_session_set_blocking(session, 0);

    try {
        const auto handshake = waitWhileAgain(m_socket, session, deadline, "SSH handshake", [&] {
            return libssh2_session_handshake(session, m_socket);
        });
        if (handshake != 0) {
            throw std::runtime_error("SSH handshake with " + m_config.host + ":" + std::to_string(m_config.port)
                + " failed: " + lastSessionError(session));
        }

        verifyHostKey();

        const auto authMethods = queryAuthMethods(m_socket, session, m_config.username, deadline);
        const auto offers = [&authMethods](const char* method) {
            return authMethods.empty() || authMethods.find(method) != std::string::npos;
        };

        int authResult = LIBSSH2_ERROR_AUTHENTICATION_FAILED;
        if (m_config.authMethod == RemoteAuthMethod::Key) {
            if (!offers("publickey")) {
                throw std::runtime_error("Server does not accept public-key authentication for user '"
                    + m_config.username + "'. It offers: " + authMethods + ".");
            }
            const auto publicKeyPath = m_config.privateKeyPath.string() + ".pub";
            const auto privateKeyPath = m_config.privateKeyPath.string();
            authResult = waitWhileAgain(m_socket, session, deadline, "SSH public-key authentication", [&] {
                return libssh2_userauth_publickey_fromfile(
                    session,
                    m_config.username.c_str(),
                    std::filesystem::exists(publicKeyPath) ? publicKeyPath.c_str() : nullptr,
                    privateKeyPath.c_str(),
                    m_config.privateKeyPassphrase.empty() ? nullptr : m_config.privateKeyPassphrase.c_str());
            });
        } else {
            const bool hasPassword = offers("password");
            const bool hasKeyboard = offers("keyboard-interactive");
            if (!hasPassword && !hasKeyboard) {
                throw std::runtime_error("Server does not accept password authentication for user '"
                    + m_config.username + "'. It offers: " + authMethods + ".");
            }
            if (hasPassword) {
                authResult = waitWhileAgain(m_socket, session, deadline, "SSH password authentication", [&] {
                    return libssh2_userauth_password(session, m_config.username.c_str(), m_config.password.c_str());
                });
            }
            if (authResult != 0 && hasKeyboard) {
                void** abstract = libssh2_session_abstract(session);
                *abstract = &m_config.password;
                authResult = waitWhileAgain(m_socket, session, deadline, "SSH keyboard-interactive authentication", [&] {
                    return libssh2_userauth_keyboard_interactive(session, m_config.username.c_str(), &keyboardInteractiveCallback);
                });
            }
        }

        if (authResult != 0) {
            throw std::runtime_error("SSH authentication failed for user '" + m_config.username + "': "
                + lastSessionError(session));
        }
        m_connected = true;
    } catch (...) {
        closeSession();
        throw;
    }
}

auto SshClient::verifyHostKey() -> void
{
    auto* session = sessionFromVoid(m_session);
    auto* knownHosts = libssh2_knownhost_init(session);
    if (knownHosts == nullptr) throw std::runtime_error("Could not initialize SSH known-hosts store.");

    std::filesystem::path knownHostsPath = m_config.knownHostsFile;
    if (knownHostsPath.empty()) {
        if (const char* home = std::getenv("HOME"); home != nullptr) {
            knownHostsPath = std::filesystem::path { home } / ".ssh" / "known_hosts";
        }
    }

    if (!knownHostsPath.empty() && std::filesystem::exists(knownHostsPath)) {
        if (libssh2_knownhost_readfile(knownHosts, knownHostsPath.string().c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) < 0) {
            libssh2_knownhost_free(knownHosts);
            throw std::runtime_error("Could not read known_hosts file: " + knownHostsPath.string());
        }
    }

    std::size_t keyLength {};
    int keyType {};
    const char* hostKey = libssh2_session_hostkey(session, &keyLength, &keyType);
    static_cast<void>(keyType);
    if (hostKey == nullptr) {
        libssh2_knownhost_free(knownHosts);
        throw std::runtime_error("Could not obtain remote host key for verification.");
    }

    const auto check = libssh2_knownhost_checkp(
        knownHosts,
        m_config.host.c_str(),
        m_config.port,
        hostKey,
        keyLength,
        LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
        nullptr);
    libssh2_knownhost_free(knownHosts);

    if (check == LIBSSH2_KNOWNHOST_CHECK_MATCH) {
        Logger::info("Remote host key verified against known_hosts.");
        return;
    }
    if (check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) {
        throw std::runtime_error("Remote host key MISMATCH for " + m_config.host + ". Connection aborted.");
    }
    if (check == LIBSSH2_KNOWNHOST_CHECK_NOTFOUND) {
        if (m_config.strictHostKey) {
            throw std::runtime_error("Remote host key for " + m_config.host + " is not present in known_hosts: "
                + (knownHostsPath.empty() ? std::string { "<not configured>" } : knownHostsPath.string()));
        }
        Logger::warning("Remote host key is unknown; proceeding because strict host-key checking is disabled.");
        return;
    }
    throw std::runtime_error("Remote host key verification failed for " + m_config.host + ".");
}

auto SshClient::execute(const std::string& command) -> SshCommandResult
{
    if (!m_connected || m_session == nullptr) throw std::runtime_error("SSH client is not connected.");
    auto* session = sessionFromVoid(m_session);
    const auto deadline = makeDeadline(m_config.commandTimeout);

    LIBSSH2_CHANNEL* channel {};
    while ((channel = libssh2_channel_open_session(session)) == nullptr) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            throw std::runtime_error("Could not open SSH channel: " + lastSessionError(session));
        }
        waitSocket(m_socket, session, deadline, "SSH channel open");
    }

    const auto freeChannel = [&] { if (channel != nullptr) { libssh2_channel_free(channel); channel = nullptr; } };
    try {
        const auto execResult = waitWhileAgain(m_socket, session, deadline, "Remote command start", [&] {
            return libssh2_channel_exec(channel, command.c_str());
        });
        if (execResult != 0) throw std::runtime_error("Could not execute remote command: " + lastSessionError(session));

        std::string output;
        std::string errorOutput;
        std::array<char, 8192> buffer {};

        while (!libssh2_channel_eof(channel)) {
            bool progress = false;

            for (;;) {
                const auto count = libssh2_channel_read(channel, buffer.data(), buffer.size());
                if (count > 0) {
                    output.append(buffer.data(), static_cast<std::size_t>(count));
                    progress = true;
                    continue;
                }
                if (count < 0 && count != LIBSSH2_ERROR_EAGAIN) {
                    throw std::runtime_error("SSH stdout read failed: " + lastSessionError(session));
                }
                break;
            }

            for (;;) {
                const auto count = libssh2_channel_read_stderr(channel, buffer.data(), buffer.size());
                if (count > 0) {
                    errorOutput.append(buffer.data(), static_cast<std::size_t>(count));
                    progress = true;
                    continue;
                }
                if (count < 0 && count != LIBSSH2_ERROR_EAGAIN) {
                    throw std::runtime_error("SSH stderr read failed: " + lastSessionError(session));
                }
                break;
            }

            if (!progress && !libssh2_channel_eof(channel)) {
                waitSocket(m_socket, session, deadline, "Remote command");
            }
        }

        const auto closeResult = waitWhileAgain(m_socket, session, deadline, "SSH channel close", [&] {
            return libssh2_channel_close(channel);
        });
        if (closeResult != 0) throw std::runtime_error("Could not close SSH channel cleanly: " + lastSessionError(session));

        const auto exitCode = libssh2_channel_get_exit_status(channel);
        freeChannel();
        return { exitCode, std::move(output), std::move(errorOutput) };
    } catch (...) {
        freeChannel();
        throw;
    }
}

auto SshClient::downloadFile(
    const std::filesystem::path& remotePath,
    const std::filesystem::path& localPath
) -> void
{
    if (!m_connected || m_session == nullptr) throw std::runtime_error("SSH client is not connected.");
    auto* session = sessionFromVoid(m_session);
    auto deadline = makeDeadline(m_config.commandTimeout);
    SftpSession sftp { session, m_socket, deadline };

    LIBSSH2_SFTP_HANDLE* handle {};
    while ((handle = libssh2_sftp_open(sftp.get(), remotePath.string().c_str(), LIBSSH2_FXF_READ, 0)) == nullptr) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            throw std::runtime_error("Could not open remote file for SFTP: " + remotePath.string());
        }
        waitSocket(m_socket, session, deadline, "SFTP remote-file open");
    }

    std::filesystem::create_directories(localPath.parent_path());
    std::ofstream output { localPath, std::ios::binary | std::ios::trunc };
    if (!output) {
        libssh2_sftp_close(handle);
        throw std::runtime_error("Could not open local file: " + localPath.string());
    }

    unsigned long long total {};
    LIBSSH2_SFTP_ATTRIBUTES attributes {};
    int statResult {};
    while ((statResult = libssh2_sftp_fstat(handle, &attributes)) == LIBSSH2_ERROR_EAGAIN) {
        waitSocket(m_socket, session, deadline, "SFTP stat");
    }
    if (statResult == 0 && (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE) != 0) total = attributes.filesize;

    const auto emitProgress = [](unsigned long long done, unsigned long long size) {
        std::fprintf(stderr, "@@PROGRESS|downloading|%llu|%llu\n", done, size);
        std::fflush(stderr);
    };

    std::array<char, 32768> buffer {};
    unsigned long long received {};
    unsigned long long lastEmit {};
    emitProgress(0, total);

    try {
        for (;;) {
            const auto count = libssh2_sftp_read(handle, buffer.data(), buffer.size());
            if (count > 0) {
                output.write(buffer.data(), count);
                if (!output) throw std::runtime_error("Local write failed: " + localPath.string());
                received += static_cast<unsigned long long>(count);
                deadline = makeDeadline(m_config.commandTimeout);
                if (received - lastEmit >= 262144) {
                    emitProgress(received, total);
                    lastEmit = received;
                }
                continue;
            }
            if (count == 0) break;
            if (count == LIBSSH2_ERROR_EAGAIN) {
                waitSocket(m_socket, session, deadline, "SFTP download");
                continue;
            }
            throw std::runtime_error("SFTP read failed: " + lastSessionError(session));
        }
    } catch (...) {
        libssh2_sftp_close(handle);
        output.close();
        std::error_code error;
        std::filesystem::remove(localPath, error);
        throw;
    }

    emitProgress(received, total != 0 ? total : received);
    libssh2_sftp_close(handle);
    output.close();
    if (!output) {
        std::error_code error;
        std::filesystem::remove(localPath, error);
        throw std::runtime_error("Could not finalize downloaded file: " + localPath.string());
    }
    if (!std::filesystem::exists(localPath) || std::filesystem::file_size(localPath) == 0) {
        std::error_code error;
        std::filesystem::remove(localPath, error);
        throw std::runtime_error("Downloaded backup file is missing or empty.");
    }
    if (total != 0 && received != total) {
        std::error_code error;
        std::filesystem::remove(localPath, error);
        throw std::runtime_error("SFTP download size mismatch.");
    }
    writeFilePermissionsOwnerOnly(localPath);
}

auto SshClient::uploadTextFile(
    const std::filesystem::path& remotePath,
    const std::string& content,
    long permissions
) -> void
{
    if (!m_connected || m_session == nullptr) throw std::runtime_error("SSH client is not connected.");
    auto* session = sessionFromVoid(m_session);
    auto deadline = makeDeadline(m_config.commandTimeout);
    SftpSession sftp { session, m_socket, deadline };

    LIBSSH2_SFTP_HANDLE* handle {};
    while ((handle = libssh2_sftp_open(
        sftp.get(), remotePath.string().c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        permissions)) == nullptr) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
            throw std::runtime_error("Could not open remote file for SFTP write: " + remotePath.string());
        }
        waitSocket(m_socket, session, deadline, "SFTP remote-file create");
    }

    std::size_t offset {};
    while (offset < content.size()) {
        const auto count = libssh2_sftp_write(handle, content.data() + offset, content.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            deadline = makeDeadline(m_config.commandTimeout);
            continue;
        }
        if (count == LIBSSH2_ERROR_EAGAIN) {
            waitSocket(m_socket, session, deadline, "SFTP upload");
            continue;
        }
        libssh2_sftp_close(handle);
        throw std::runtime_error("SFTP write failed: " + lastSessionError(session));
    }

    libssh2_sftp_close(handle);
}

auto SshClient::closeSession() noexcept -> void
{
    auto* session = sessionFromVoid(m_session);
    if (session != nullptr) {
        libssh2_session_disconnect(session, "Vaultium session closed.");
        libssh2_session_free(session);
        m_session = nullptr;
    }
    if (m_socket != -1) {
        ::close(m_socket);
        m_socket = -1;
    }
    m_connected = false;
}

} // namespace vaultium::remote
