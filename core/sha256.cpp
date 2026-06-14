module;

#include <array>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

module vaultium_core_sha256;

namespace vaultium {
namespace {

// FIPS 180-4 SHA-256 round constants.
constexpr std::array<std::uint32_t, 64> kRoundConstants {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

[[nodiscard]] constexpr auto rotateRight(std::uint32_t value, std::uint32_t bits) -> std::uint32_t
{
    return (value >> bits) | (value << (32 - bits));
}

/**
 * @brief Streaming SHA-256 state machine.
 */
class Sha256 final {
public:
    auto update(const unsigned char* data, std::size_t length) -> void
    {
        for (std::size_t index = 0; index < length; ++index) {
            m_buffer[m_bufferLength++] = data[index];

            if (m_bufferLength == 64) {
                processBlock(m_buffer.data());
                m_bitLength += 512;
                m_bufferLength = 0;
            }
        }
    }

    [[nodiscard]] auto finalizeHex() -> std::string
    {
        m_bitLength += static_cast<std::uint64_t>(m_bufferLength) * 8;

        m_buffer[m_bufferLength++] = 0x80;

        if (m_bufferLength > 56) {
            while (m_bufferLength < 64) {
                m_buffer[m_bufferLength++] = 0x00;
            }

            processBlock(m_buffer.data());
            m_bufferLength = 0;
        }

        while (m_bufferLength < 56) {
            m_buffer[m_bufferLength++] = 0x00;
        }

        for (int shift = 56; shift >= 0; shift -= 8) {
            m_buffer[m_bufferLength++] = static_cast<unsigned char>((m_bitLength >> shift) & 0xff);
        }

        processBlock(m_buffer.data());

        return toHex();
    }

private:
    std::array<std::uint32_t, 8> m_state {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    std::array<unsigned char, 64> m_buffer {};
    std::size_t m_bufferLength {};
    std::uint64_t m_bitLength {};

    auto processBlock(const unsigned char* block) -> void
    {
        std::array<std::uint32_t, 64> words {};

        for (std::size_t index = 0; index < 16; ++index) {
            words[index] =
                (static_cast<std::uint32_t>(block[index * 4]) << 24) |
                (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16) |
                (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8) |
                (static_cast<std::uint32_t>(block[index * 4 + 3]));
        }

        for (std::size_t index = 16; index < 64; ++index) {
            const auto s0 = rotateRight(words[index - 15], 7) ^ rotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
            const auto s1 = rotateRight(words[index - 2], 17) ^ rotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }

        auto a = m_state[0];
        auto b = m_state[1];
        auto c = m_state[2];
        auto d = m_state[3];
        auto e = m_state[4];
        auto f = m_state[5];
        auto g = m_state[6];
        auto h = m_state[7];

        for (std::size_t index = 0; index < 64; ++index) {
            const auto s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const auto ch = (e & f) ^ (~e & g);
            const auto temp1 = h + s1 + ch + kRoundConstants[index] + words[index];
            const auto s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    [[nodiscard]] auto toHex() const -> std::string
    {
        static constexpr char hexDigits[] = "0123456789abcdef";

        std::string result;
        result.reserve(64);

        for (const auto word : m_state) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                result.push_back(hexDigits[(word >> shift) & 0xf]);
            }
        }

        return result;
    }
};

} // namespace

auto sha256Hex(const unsigned char* data, std::size_t length) -> std::string
{
    Sha256 hasher;
    hasher.update(data, length);
    return hasher.finalizeHex();
}

auto sha256Hex(const std::string& data) -> std::string
{
    return sha256Hex(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

auto sha256File(const std::filesystem::path& path) -> std::string
{
    std::ifstream file { path, std::ios::binary };

    if (!file) {
        throw std::runtime_error("Could not open file for hashing: " + path.string());
    }

    Sha256 hasher;
    std::array<char, 65536> buffer {};

    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto readCount = file.gcount();

        if (readCount > 0) {
            hasher.update(
                reinterpret_cast<const unsigned char*>(buffer.data()),
                static_cast<std::size_t>(readCount)
            );
        }
    }

    if (file.bad()) {
        throw std::runtime_error("Error reading file for hashing: " + path.string());
    }

    return hasher.finalizeHex();
}

} // namespace vaultium
