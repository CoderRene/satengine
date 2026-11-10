#include "PakCrypto.hpp"
#include <limits>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <memory>
#endif
namespace sat {
void clearPakKey(PakKey& key) {
#if defined(_WIN32)
    SecureZeroMemory(key.data(), key.size());
#else
    OPENSSL_cleanse(key.data(), key.size());
#endif
}
bool pakRandom(std::span<unsigned char> bytes) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) { return false; }
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#else
    return RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) == 1;
#endif
}
bool pakCrypt(bool encrypt, const PakKey& key, std::string_view nonce,
              std::string_view aad, std::string_view input, std::string& output,
              std::span<unsigned char, 16> tag) {
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) { return false; }
    output.resize(input.size() + 16);
#if defined(_WIN32)
    struct Algorithm {
        BCRYPT_ALG_HANDLE handle = nullptr;
        ~Algorithm() { if (handle) { BCryptCloseAlgorithmProvider(handle, 0); } }
    } algorithm;
    struct Key {
        BCRYPT_KEY_HANDLE handle = nullptr;
        ~Key() { if (handle) { BCryptDestroyKey(handle); } }
    } cipherKey;
    if (BCryptOpenAlgorithmProvider(&algorithm.handle, BCRYPT_AES_ALGORITHM, nullptr, 0) < 0 ||
        BCryptSetProperty(algorithm.handle, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0) { return false; }
    if (BCryptGenerateSymmetricKey(algorithm.handle, &cipherKey.handle, nullptr, 0,
            const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0) < 0) { return false; }
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.data()));
    info.cbNonce = static_cast<ULONG>(nonce.size());
    info.pbAuthData = reinterpret_cast<PUCHAR>(const_cast<char*>(aad.data()));
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = tag.data(); info.cbTag = 16;
    ULONG written = 0;
    const auto function = encrypt ? BCryptEncrypt : BCryptDecrypt;
    const auto status = function(cipherKey.handle, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
        static_cast<ULONG>(input.size()), &info, nullptr, 0, reinterpret_cast<PUCHAR>(output.data()),
        static_cast<ULONG>(output.size()), &written, 0);
    if (status < 0) { output.clear(); return false; }
    output.resize(written);
#else
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context || EVP_CipherInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(),
        reinterpret_cast<const unsigned char*>(nonce.data()), encrypt ? 1 : 0) != 1) { return false; }
    int written = 0;
    if (EVP_CipherUpdate(context.get(), nullptr, &written,
        reinterpret_cast<const unsigned char*>(aad.data()), static_cast<int>(aad.size())) != 1) { return false; }
    if (EVP_CipherUpdate(context.get(), reinterpret_cast<unsigned char*>(output.data()), &written,
        reinterpret_cast<const unsigned char*>(input.data()), static_cast<int>(input.size())) != 1) { return false; }
    const int size = written;
    if (!encrypt && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1) { return false; }
    if (EVP_CipherFinal_ex(context.get(), reinterpret_cast<unsigned char*>(output.data()) + size, &written) != 1) {
        output.clear(); return false;
    }
    output.resize(static_cast<std::size_t>(size + written));
    if (encrypt && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) { return false; }
#endif
    return true;
}
}
