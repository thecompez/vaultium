module;

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include <libssh2.h>
#include <libssh2_sftp.h>

module vaultium_remote_ssh_client;

import vaultium_core_types;
import vaultium_core_logger;

namespace vaultium::remote {
namespace {

class LibSsh2Global final {
public:
    LibSsh2Global()
    {
        if (libssh2_init(0) != 0) {
            throw std::runtime_error("libssh2 initialization failed.");
        }
    }

    ~LibSsh2Global()
    {
        libssh2_exit();
    }
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

    if (length <= 0 || message == nullptr) {
        return "Unknown libssh2 error.";
    }

    return std::string { message, static_cast<std::size_t>(length) };
}

[[nodiscard]] auto waitSocket(int socketFd, LIBSSH2_SESSION* session) -> int
{
    pollfd descriptor {};
    descriptor.fd = socketFd;
    descriptor.events = 0;

    const auto direction = libssh2_session_block_directions(session);

    if ((direction & LIBSSH2_SESSION_BLOCK_INBOUND) != 0) {
        descriptor.events |= POLLIN;
    }

    if ((direction & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0) {
        descriptor.events |= POLLOUT;
    }

    return poll(&descriptor, 1, 10000);
}

auto waitWhileAgain(int socketFd, LIBSSH2_SESSION* session, auto&& operation) -> int
{
    int result {};

    while ((result = operation()) == LIBSSH2_ERROR_EAGAIN) {
        if (waitSocket(socketFd, session) < 0) {
            throw std::runtime_error("Socket polling failed.");
        }
    }

    return result;
}

[[nodiscard]] auto openTcpSocket(
    const std::string& host,
    std::uint16_t port
) -> int
{
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result {};

    const auto portString = std::to_string(port);
    const auto status = getaddrinfo(host.c_str(), portString.c_str(), &hints, &result);

    if (status != 0) {
        throw std::runtime_error("getaddrinfo failed: " + std::string { gai_strerror(status) });
    }

    int socketFd = -1;

    for (auto* item = result; item != nullptr; item = item->ai_next) {
        socketFd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);

        if (socketFd == -1) {
            continue;
        }

        if (::connect(socketFd, item->ai_addr, item->ai_addrlen) == 0) {
            break;
        }

        close(socketFd);
        socketFd = -1;
    }

    freeaddrinfo(result);

    if (socketFd == -1) {
        throw std::runtime_error("Could not connect to " + host + ":" + std::to_string(port));
    }

    return socketFd;
}

// Returns the comma-separated auth methods the server permits for `username`
// (e.g. "publickey,password,keyboard-interactive"), or empty on error.
[[nodiscard]] auto queryAuthMethods(
    int socketFd,
    LIBSSH2_SESSION* session,
    const std::string& username) -> std::string
{
    while (true) {
        char* list = libssh2_userauth_list(
            session, username.c_str(), static_cast<unsigned int>(username.length()));

        if (list != nullptr) {
            return std::string { list };
        }

        if (libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
            if (waitSocket(socketFd, session) < 0) {
                return {};
            }
            continue;
        }

        return {};
    }
}

auto writeFilePermissionsOwnerOnly(const std::filesystem::path& path) -> void
{
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace
    );
}

// Keyboard-interactive auth callback. The password is passed through the libssh2
// session "abstract" pointer. Responses must be malloc'd; libssh2 frees them.
extern "C" void keyboardInteractiveCallback(
    const char* /*name*/, int /*nameLength*/,
    const char* /*instruction*/, int /*instructionLength*/,
    int numPrompts,
    const LIBSSH2_USERAUTH_KBDINT_PROMPT* /*prompts*/,
    LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
    void** abstract)
{
    const auto* password = static_cast<const std::string*>(*abstract);

    for (int index = 0; index < numPrompts; ++index) {
        if (password == nullptr) {
            continue;
        }

        responses[index].text = static_cast<char*>(std::malloc(password->size() + 1));

        if (responses[index].text != nullptr) {
            std::memcpy(responses[index].text, password->c_str(), password->size() + 1);
            responses[index].length = static_cast<unsigned int>(password->size());
        }
    }
}

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

    m_socket = openTcpSocket(m_config.host, m_config.port);

    auto* session = libssh2_session_init();

    if (session == nullptr) {
        close(m_socket);
        m_socket = -1;
        throw std::runtime_error("Could not initialize SSH session.");
    }

    m_session = session;

    libssh2_session_set_blocking(session, 0);

    const auto handshakeResult = waitWhileAgain(m_socket, session, [&] {
        return libssh2_session_handshake(session, m_socket);
    });

    if (handshakeResult != 0) {
        auto detail = lastSessionError(session);
        if (detail.empty() || detail == "Unknown libssh2 error.") {
            detail = "the server closed the connection during the SSH handshake. Confirm the "
                     "host and port are correct and reachable (a wrong port often lands on a "
                     "non-SSH service), then try again.";
        }
        throw std::runtime_error(
            "SSH handshake with " + m_config.host + ":" + std::to_string(m_config.port) +
            " failed: " + detail);
    }

    verifyHostKey();

    // Ask the server which methods it actually accepts for this user, so we pick
    // the right one and can give a precise error if our choice is not offered.
    const auto authMethods = queryAuthMethods(m_socket, session, m_config.username);
    const auto offers = [&authMethods](const char* method) {
        return authMethods.empty() || authMethods.find(method) != std::string::npos;
    };

    int authResult {};

    if (m_config.authMethod == RemoteAuthMethod::Key) {
        if (!offers("publickey")) {
            throw std::runtime_error(
                "Server does not accept public-key authentication for user '" +
                m_config.username + "'. It offers: " + authMethods + ".");
        }

        const auto publicKeyPath = m_config.privateKeyPath.string() + ".pub";
        const auto privateKeyPath = m_config.privateKeyPath.string();

        authResult = waitWhileAgain(m_socket, session, [&] {
            return libssh2_userauth_publickey_fromfile(
                session,
                m_config.username.c_str(),
                std::filesystem::exists(publicKeyPath) ? publicKeyPath.c_str() : nullptr,
                privateKeyPath.c_str(),
                m_config.privateKeyPassphrase.empty() ? nullptr : m_config.privateKeyPassphrase.c_str()
            );
        });
    } else {
        const bool hasPassword = offers("password");
        const bool hasKeyboard = offers("keyboard-interactive");

        if (!hasPassword && !hasKeyboard) {
            throw std::runtime_error(
                "Server does not accept password authentication for user '" +
                m_config.username + "'. It offers: " + authMethods +
                ". Use key-based authentication, or enable password login on the server "
                "(for root, set 'PermitRootLogin yes' in sshd_config).");
        }

        if (hasPassword) {
            authResult = waitWhileAgain(m_socket, session, [&] {
                return libssh2_userauth_password(
                    session,
                    m_config.username.c_str(),
                    m_config.password.c_str()
                );
            });
        }

        // Many servers (e.g. Ubuntu with PAM) only offer keyboard-interactive and
        // reject the plain password method. Use it (as primary or fallback),
        // feeding the same password.
        if (authResult != 0 && hasKeyboard) {
            void** abstract = libssh2_session_abstract(session);
            *abstract = &m_config.password;

            authResult = waitWhileAgain(m_socket, session, [&] {
                return libssh2_userauth_keyboard_interactive(
                    session,
                    m_config.username.c_str(),
                    &keyboardInteractiveCallback
                );
            });
        }
    }

    if (authResult != 0) {
        const auto errorCode = libssh2_session_last_errno(session);
        auto detail = lastSessionError(session);

        if (detail.empty() || detail == "Unknown libssh2 error.") {
            detail = m_config.authMethod == RemoteAuthMethod::Key
                ? "the server rejected the key, or the user is not permitted to log in with it."
                : "the password was rejected by the server.";
        }

        const auto offered = authMethods.empty()
            ? std::string {}
            : " The server offers: " + authMethods + ".";

        throw std::runtime_error(
            "SSH authentication failed for user '" + m_config.username + "': " + detail +
            offered + " (libssh2 code " + std::to_string(errorCode) + ")"
        );
    }

    m_connected = true;
}

auto SshClient::verifyHostKey() -> void
{
    auto* session = sessionFromVoid(m_session);

    auto* knownHosts = libssh2_knownhost_init(session);

    if (knownHosts == nullptr) {
        throw std::runtime_error("Could not initialize SSH known-hosts store.");
    }

    std::filesystem::path knownHostsPath = m_config.knownHostsFile;

    if (knownHostsPath.empty()) {
        if (const char* home = std::getenv("HOME"); home != nullptr) {
            knownHostsPath = std::filesystem::path { home } / ".ssh" / "known_hosts";
        }
    }

    const auto knownHostsExists =
        !knownHostsPath.empty() && std::filesystem::exists(knownHostsPath);

    if (knownHostsExists) {
        const auto readResult = libssh2_knownhost_readfile(
            knownHosts,
            knownHostsPath.string().c_str(),
            LIBSSH2_KNOWNHOST_FILE_OPENSSH
        );

        if (readResult < 0) {
            libssh2_knownhost_free(knownHosts);
            throw std::runtime_error("Could not read known_hosts file: " + knownHostsPath.string());
        }
    }

    std::size_t keyLength {};
    int keyType {};

    const char* hostKey = libssh2_session_hostkey(session, &keyLength, &keyType);

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
        nullptr
    );

    libssh2_knownhost_free(knownHosts);

    switch (check) {
    case LIBSSH2_KNOWNHOST_CHECK_MATCH:
        Logger::info("Remote host key verified against known_hosts.");
        return;

    case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
        throw std::runtime_error(
            "Remote host key MISMATCH for " + m_config.host +
            ". The key differs from known_hosts. This may indicate a "
            "man-in-the-middle attack. Connection aborted."
        );

    case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
        if (m_config.strictHostKey) {
            throw std::runtime_error(
                "Remote host key for " + m_config.host +
                " is not in known_hosts (" +
                (knownHostsPath.empty() ? "no known_hosts file configured" : knownHostsPath.string()) +
                "). Add it first, e.g.: ssh-keyscan -p " +
                std::to_string(m_config.port) + " " + m_config.host +
                " >> ~/.ssh/known_hosts  (or set REMOTE_STRICT_HOST_KEY=false to trust on first use)."
            );
        }

        Logger::warning(
            "Remote host key for " + m_config.host +
            " is not in known_hosts. Proceeding because strict host-key checking is disabled."
        );
        return;

    case LIBSSH2_KNOWNHOST_CHECK_FAILURE:
    default:
        throw std::runtime_error("Remote host key verification failed for " + m_config.host + ".");
    }
}

auto SshClient::execute(const std::string& command) -> SshCommandResult
{
    if (!m_connected || m_session == nullptr) {
        throw std::runtime_error("SSH client is not connected.");
    }

    auto* session = sessionFromVoid(m_session);
    LIBSSH2_CHANNEL* channel {};

    while ((channel = libssh2_channel_open_session(session)) == nullptr) {
        const auto error = libssh2_session_last_errno(session);

        if (error != LIBSSH2_ERROR_EAGAIN) {
            throw std::runtime_error("Could not open SSH channel: " + lastSessionError(session));
        }

        static_cast<void>(waitSocket(m_socket, session));
    }

    const auto execResult = waitWhileAgain(m_socket, session, [&] {
        return libssh2_channel_exec(channel, command.c_str());
    });

    if (execResult != 0) {
        libssh2_channel_free(channel);
        throw std::runtime_error("Could not execute remote command: " + lastSessionError(session));
    }

    std::string output;
    std::string errorOutput;

    std::array<char, 8192> buffer {};

    while (true) {
        const auto readCount = libssh2_channel_read(channel, buffer.data(), buffer.size());

        if (readCount > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(readCount));
            continue;
        }

        if (readCount == LIBSSH2_ERROR_EAGAIN) {
            static_cast<void>(waitSocket(m_socket, session));
            continue;
        }

        break;
    }

    while (true) {
        const auto readCount = libssh2_channel_read_stderr(channel, buffer.data(), buffer.size());

        if (readCount > 0) {
            errorOutput.append(buffer.data(), static_cast<std::size_t>(readCount));
            continue;
        }

        if (readCount == LIBSSH2_ERROR_EAGAIN) {
            static_cast<void>(waitSocket(m_socket, session));
            continue;
        }

        break;
    }

    waitWhileAgain(m_socket, session, [&] {
        return libssh2_channel_close(channel);
    });

    const auto exitCode = libssh2_channel_get_exit_status(channel);

    libssh2_channel_free(channel);

    return SshCommandResult {
        .exitCode = exitCode,
        .output = std::move(output),
        .errorOutput = std::move(errorOutput)
    };
}

auto SshClient::downloadFile(
    const std::filesystem::path& remotePath,
    const std::filesystem::path& localPath
) -> void
{
    if (!m_connected || m_session == nullptr) {
        throw std::runtime_error("SSH client is not connected.");
    }

    auto* session = sessionFromVoid(m_session);

    LIBSSH2_SFTP* sftp {};

    while ((sftp = libssh2_sftp_init(session)) == nullptr) {
        const auto error = libssh2_session_last_errno(session);

        if (error != LIBSSH2_ERROR_EAGAIN) {
            throw std::runtime_error("Could not initialize SFTP session: " + lastSessionError(session));
        }

        static_cast<void>(waitSocket(m_socket, session));
    }

    LIBSSH2_SFTP_HANDLE* handle {};

    while ((handle = libssh2_sftp_open(
        sftp,
        remotePath.string().c_str(),
        LIBSSH2_FXF_READ,
        0
    )) == nullptr) {
        const auto error = libssh2_session_last_errno(session);

        if (error != LIBSSH2_ERROR_EAGAIN) {
            libssh2_sftp_shutdown(sftp);
            throw std::runtime_error("Could not open remote file for SFTP: " + remotePath.string());
        }

        static_cast<void>(waitSocket(m_socket, session));
    }

    std::filesystem::create_directories(localPath.parent_path());

    std::ofstream output { localPath, std::ios::binary | std::ios::trunc };

    if (!output) {
        libssh2_sftp_close(handle);
        libssh2_sftp_shutdown(sftp);
        throw std::runtime_error("Could not open local file: " + localPath.string());
    }

    // Determine the total size so the client can show real download progress.
    unsigned long long total = 0;
    {
        LIBSSH2_SFTP_ATTRIBUTES attrs {};
        int rc = 0;
        while ((rc = libssh2_sftp_fstat(handle, &attrs)) == LIBSSH2_ERROR_EAGAIN) {
            static_cast<void>(waitSocket(m_socket, session));
        }
        if (rc == 0 && (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) != 0) {
            total = attrs.filesize;
        }
    }

    // Progress events go to stderr with a machine-readable prefix; the GUI parses
    // them. Throttled to ~256 KB so large files don't flood the channel.
    const auto emitProgress = [](unsigned long long done, unsigned long long t) {
        std::fprintf(stderr, "@@PROGRESS|downloading|%llu|%llu\n", done, t);
        std::fflush(stderr);
    };

    unsigned long long received = 0;
    unsigned long long lastEmit = 0;
    emitProgress(0, total);

    std::array<char, 32768> buffer {};

    while (true) {
        const auto readCount = libssh2_sftp_read(handle, buffer.data(), buffer.size());

        if (readCount > 0) {
            output.write(buffer.data(), readCount);
            received += static_cast<unsigned long long>(readCount);
            if (received - lastEmit >= 262144) {
                emitProgress(received, total);
                lastEmit = received;
            }
            continue;
        }

        if (readCount == LIBSSH2_ERROR_EAGAIN) {
            static_cast<void>(waitSocket(m_socket, session));
            continue;
        }

        if (readCount < 0) {
            libssh2_sftp_close(handle);
            libssh2_sftp_shutdown(sftp);
            throw std::runtime_error("SFTP read failed.");
        }

        break;
    }

    emitProgress(received, total != 0 ? total : received);
    output.close();

    libssh2_sftp_close(handle);
    libssh2_sftp_shutdown(sftp);

    if (!std::filesystem::exists(localPath) || std::filesystem::file_size(localPath) == 0) {
        throw std::runtime_error("Downloaded backup file is missing or empty.");
    }

    writeFilePermissionsOwnerOnly(localPath);
}

    auto SshClient::uploadTextFile(
    const std::filesystem::path& remotePath,
    const std::string& content,
    long permissions
) -> void
{
    if (!m_connected || m_session == nullptr) {
        throw std::runtime_error("SSH client is not connected.");
    }

    auto* session = sessionFromVoid(m_session);

    LIBSSH2_SFTP* sftp {};

    while ((sftp = libssh2_sftp_init(session)) == nullptr) {
        const auto error = libssh2_session_last_errno(session);

        if (error != LIBSSH2_ERROR_EAGAIN) {
            throw std::runtime_error("Could not initialize SFTP session: " + lastSessionError(session));
        }

        static_cast<void>(waitSocket(m_socket, session));
    }

    LIBSSH2_SFTP_HANDLE* handle {};

    while ((handle = libssh2_sftp_open(
        sftp,
        remotePath.string().c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        permissions
    )) == nullptr) {
        const auto error = libssh2_session_last_errno(session);

        if (error != LIBSSH2_ERROR_EAGAIN) {
            libssh2_sftp_shutdown(sftp);
            throw std::runtime_error("Could not open remote file for SFTP write: " + remotePath.string());
        }

        static_cast<void>(waitSocket(m_socket, session));
    }

    std::size_t offset {};

    while (offset < content.size()) {
        const auto remaining = content.size() - offset;
        const auto chunkSize = std::min<std::size_t>(remaining, 32768);

        const auto writeCount = libssh2_sftp_write(
            handle,
            content.data() + offset,
            chunkSize
        );

        if (writeCount > 0) {
            offset += static_cast<std::size_t>(writeCount);
            continue;
        }

        if (writeCount == LIBSSH2_ERROR_EAGAIN) {
            static_cast<void>(waitSocket(m_socket, session));
            continue;
        }

        libssh2_sftp_close(handle);
        libssh2_sftp_shutdown(sftp);
        throw std::runtime_error("SFTP write failed.");
    }

    libssh2_sftp_close(handle);
    libssh2_sftp_shutdown(sftp);
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
        close(m_socket);
        m_socket = -1;
    }

    m_connected = false;
}

} // namespace vaultium::remote