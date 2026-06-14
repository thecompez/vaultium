module;

#include <cstdint>
#include <filesystem>
#include <string>

export module vaultium_core_sha256;

export namespace vaultium {

    /**
     * @brief Computes the SHA-256 digest of a byte buffer.
     *
     * @param data Input bytes.
     * @param length Number of bytes.
     * @return Lowercase hex-encoded 64-character digest.
     */
    [[nodiscard]] auto sha256Hex(const unsigned char* data, std::size_t length) -> std::string;

    /**
     * @brief Computes the SHA-256 digest of a string.
     *
     * @param data Input string.
     * @return Lowercase hex-encoded 64-character digest.
     */
    [[nodiscard]] auto sha256Hex(const std::string& data) -> std::string;

    /**
     * @brief Computes the SHA-256 digest of a file, streamed in chunks.
     *
     * @param path File to hash.
     * @return Lowercase hex-encoded 64-character digest.
     * @throws std::runtime_error when the file cannot be read.
     */
    [[nodiscard]] auto sha256File(const std::filesystem::path& path) -> std::string;

} // namespace vaultium
