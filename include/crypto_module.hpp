#pragma once
#include <cstdint>
#include <sodium.h>
#include <span>
#include <memory_defs.hpp>
#include <assert.h>
#include <cstring>
#include <memory>
#include <absl/strings/str_format.h>

class CryptoModule
{
public:
    // override these functions
    virtual void encrypt(const byte_t *key, const byte_t *nonce, const byte_t *message, std::size_t length, byte_t *cipher_text, byte_t *auth_tag) = 0;
    virtual bool decrypt(const byte_t *key, const byte_t *nonce, const byte_t *cipher_text, std::size_t length, const byte_t *auth_tag, byte_t *message) = 0;

public:
    [[nodiscard]] virtual std::size_t key_size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t nonce_size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t auth_tag_size() const noexcept = 0;
    [[nodiscard]] virtual std::string name() const noexcept = 0;
    virtual void random(byte_t *buf, std::size_t length) const noexcept {
        randombytes_buf(buf, length);
    }

    // implemented in base class, not virtual don't override
    inline void encrypt(std::span<const byte_t> key, std::span<const byte_t> nonce, std::span<const byte_t> message, std::span<byte_t> cipher_text, std::span<byte_t> auth_tag)
    {
        // check key and nonce are correct
        assert(key.size() == this->key_size());
        assert(nonce.size() == this->nonce_size());

        // check cipher text buffer is at least as big as message
        assert(cipher_text.size() == message.size());
        assert(auth_tag.size() == this->auth_tag_size());

        this->encrypt(key.data(), nonce.data(), message.data(), message.size(), cipher_text.data(), auth_tag.data());
    }

    inline bool decrypt(std::span<const byte_t> key, std::span<const byte_t> nonce, std::span<const byte_t> cipher_text, std::span<const byte_t> auth_tag, std::span<byte_t> message)
    {
        // check key and nonce are correct
        assert(key.size() == this->key_size());
        assert(nonce.size() == this->nonce_size());

        // check cipher text buffer is at least as big as message
        assert(cipher_text.size() == message.size());
        assert(auth_tag.size() == this->auth_tag_size());

        return this->decrypt(key.data(), nonce.data(), cipher_text.data(), cipher_text.size(), auth_tag.data(), message.data());
    }

    virtual ~CryptoModule() noexcept = default;
};


class AEGIS256Module : public CryptoModule
{
public:
AEGIS256Module() {
    if(sodium_init() == -1) {
        throw std::runtime_error("Libsodium init failed!");
    }
}

public:
    virtual void encrypt(const byte_t *key, const byte_t *nonce, const byte_t *message, std::size_t length, byte_t *cipher_text, byte_t *auth_tag) override
    {
        crypto_aead_aegis256_encrypt_detached(
            cipher_text,
            auth_tag,
            NULL,
            message,
            length,
            NULL,
            0,
            NULL,
            nonce,
            key);
    }
    virtual bool decrypt(const byte_t *key, const byte_t *nonce, const byte_t *cipher_text, std::size_t length, const byte_t *auth_tag, byte_t *message) override
    {
        auto ret_code = crypto_aead_aegis256_decrypt_detached(
            message,
            NULL,
            cipher_text,
            length,
            auth_tag,
            NULL,
            0,
            nonce,
            key
        );

        return ret_code != -1;
    }

public:
    [[nodiscard]] virtual std::size_t key_size() const noexcept
    {
        return crypto_aead_aegis256_KEYBYTES;
    }

    [[nodiscard]] virtual std::size_t nonce_size() const noexcept
    {
        return crypto_aead_aegis256_NPUBBYTES;
    }

    [[nodiscard]] virtual std::size_t auth_tag_size() const noexcept
    {
        return crypto_aead_aegis256_ABYTES;
    };

    [[nodiscard]] virtual std::string name() const noexcept {
        return "AEGIS256";
    }

    virtual ~AEGIS256Module() noexcept = default;
};

class PlainTextModule : public CryptoModule
{

protected:
    virtual void encrypt(const byte_t *key, const byte_t *nonce, const byte_t *message, std::size_t length, byte_t *cipher_text, byte_t *auth_tag) override
    {
        std::memcpy(cipher_text, message, length);
        std::memcpy(auth_tag, nonce, 32);
    }
    virtual bool decrypt(const byte_t *key, const byte_t *nonce, const byte_t *cipher_text, std::size_t length, const byte_t *auth_tag, byte_t *message) override
    {
        std::memcpy(message, cipher_text, length);
        return std::memcmp(nonce, auth_tag, 32) == 0;
    }

public:
    [[nodiscard]] virtual std::size_t key_size() const noexcept
    {
        return 32U;
    }

    [[nodiscard]] virtual std::size_t nonce_size() const noexcept
    {
        return 32U;
    }

    [[nodiscard]] virtual std::size_t auth_tag_size() const noexcept
    {
        return 32U;
    };

    [[nodiscard]] virtual std::string name() const noexcept {
        return "PlainText";
    }

    virtual ~PlainTextModule() noexcept = default;
};

inline std::unique_ptr<CryptoModule> get_crypto_module_by_name(std::string_view name) {
    if (name == "PlainText") {
        return std::make_unique<PlainTextModule>();
    } else if (name == "AGEIS256" || name == "AEGIS256") {
        // maintain compatiblity with the typo version :(
        return std::make_unique<AEGIS256Module>();
    } else {
        throw std::runtime_error(absl::StrFormat("Unkown Crypto Module name %s", name));
    }
}