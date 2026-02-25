#include "sha256.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace snap {

namespace {

constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32U - shift));
}

class Sha256 {
public:
    Sha256() {
        state_ = {
            0x6a09e667,
            0xbb67ae85,
            0x3c6ef372,
            0xa54ff53a,
            0x510e527f,
            0x9b05688c,
            0x1f83d9ab,
            0x5be0cd19,
        };
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            block_[block_len_++] = data[i];
            if (block_len_ == block_.size()) {
                transform();
                bit_len_ += 512ULL;
                block_len_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> finalize() {
        uint32_t i = block_len_;

        if (block_len_ < 56) {
            block_[i++] = 0x80;
            while (i < 56) block_[i++] = 0x00;
        } else {
            block_[i++] = 0x80;
            while (i < 64) block_[i++] = 0x00;
            transform();
            block_.fill(0);
        }

        bit_len_ += static_cast<uint64_t>(block_len_) * 8ULL;
        block_[63] = static_cast<uint8_t>(bit_len_);
        block_[62] = static_cast<uint8_t>(bit_len_ >> 8U);
        block_[61] = static_cast<uint8_t>(bit_len_ >> 16U);
        block_[60] = static_cast<uint8_t>(bit_len_ >> 24U);
        block_[59] = static_cast<uint8_t>(bit_len_ >> 32U);
        block_[58] = static_cast<uint8_t>(bit_len_ >> 40U);
        block_[57] = static_cast<uint8_t>(bit_len_ >> 48U);
        block_[56] = static_cast<uint8_t>(bit_len_ >> 56U);
        transform();

        std::array<uint8_t, 32> digest{};
        for (size_t j = 0; j < 4; ++j) {
            digest[j]      = static_cast<uint8_t>((state_[0] >> (24 - j * 8)) & 0x000000ffU);
            digest[j + 4]  = static_cast<uint8_t>((state_[1] >> (24 - j * 8)) & 0x000000ffU);
            digest[j + 8]  = static_cast<uint8_t>((state_[2] >> (24 - j * 8)) & 0x000000ffU);
            digest[j + 12] = static_cast<uint8_t>((state_[3] >> (24 - j * 8)) & 0x000000ffU);
            digest[j + 16] = static_cast<uint8_t>((state_[4] >> (24 - j * 8)) & 0x000000ffU);
            digest[j + 20] = static_cast<uint8_t>((state_[5] >> (24 - j * 8)) & 0x000000ffU);
            digest[j + 24] = static_cast<uint8_t>((state_[6] >> (24 - j * 8)) & 0x000000ffU);
            digest[j + 28] = static_cast<uint8_t>((state_[7] >> (24 - j * 8)) & 0x000000ffU);
        }
        return digest;
    }

private:
    void transform() {
        std::array<uint32_t, 64> words{};
        for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
            words[i] = (static_cast<uint32_t>(block_[j]) << 24)
                     | (static_cast<uint32_t>(block_[j + 1]) << 16)
                     | (static_cast<uint32_t>(block_[j + 2]) << 8)
                     | (static_cast<uint32_t>(block_[j + 3]));
        }

        for (uint32_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(words[i - 15], 7U) ^ rotr(words[i - 15], 18U) ^ (words[i - 15] >> 3U);
            const uint32_t s1 = rotr(words[i - 2], 17U) ^ rotr(words[i - 2], 19U) ^ (words[i - 2] >> 10U);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];

        for (uint32_t i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + ch + K[i] + words[i];
            const uint32_t s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint8_t, 64> block_{};
    uint32_t block_len_ = 0;
    uint64_t bit_len_ = 0;
    std::array<uint32_t, 8> state_{};
};

} // namespace

std::string sha256_file_hex(const std::filesystem::path& file_path) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) return "";

    Sha256 sha;
    std::array<char, 8192> buffer{};
    while (in.good()) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        if (count > 0) {
            sha.update(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(count));
        }
    }

    const auto digest = sha.finalize();
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint8_t byte : digest) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

} // namespace snap
