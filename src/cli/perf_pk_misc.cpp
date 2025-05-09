/*
* (C) 2024 Jack Lloyd
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#include "perf.h"

#if defined(BOTAN_HAS_PUBLIC_KEY_CRYPTO)
   #include <botan/assert.h>
   #include <botan/pk_algs.h>
   #include <botan/pkcs8.h>
   #include <botan/x509_key.h>
   #include <botan/internal/fmt.h>
#endif

#if defined(BOTAN_HAS_ECDSA)
   #include <botan/ecdsa.h>
   #include <botan/pubkey.h>
#endif

namespace Botan_CLI {

#if defined(BOTAN_HAS_PUBLIC_KEY_CRYPTO)

class PerfTest_PKKeyParsing final : public PerfTest {
   public:
      void go(const PerfConfig& config) override {
         const auto runtime = config.runtime();
         auto& rng = config.rng();

         const std::pair<std::string, std::string> keygen_algos[] = {
            {"RSA", "2048"},
            {"ECDSA", "secp256r1"},
            {"ECDSA", "brainpool512r1"},
            {"DH", "modp/ietf/2048"},
            {"X25519", ""},
            {"Ed25519", ""},
            {"ML-DSA", "ML-DSA-6x5"},
            {"ML-KEM", "ML-KEM-768"},
         };

         for(const auto& [algo, params] : keygen_algos) {
            auto sk = Botan::create_private_key(algo, rng, params);

            if(!sk) {
               continue;
            }

            const auto pk = sk->public_key();

            const std::string nm = params.empty() ? algo : Botan::fmt("{} {}", algo, params);

            auto pk_parse = config.make_timer(nm, 1, "public key parse");
            const auto pk_bytes = pk->subject_public_key();
            pk_parse->run_until_elapsed(runtime, [&]() { Botan::X509::load_key(pk_bytes); });
            config.record_result(*pk_parse);

            auto sk_parse = config.make_timer(nm, 1, "private key parse");
            const auto sk_bytes = sk->private_key_info();
            sk_parse->run_until_elapsed(runtime, [&]() { Botan::PKCS8::load_key(sk_bytes); });
            config.record_result(*sk_parse);
         }
      }
};

BOTAN_REGISTER_PERF_TEST("key_parsing", PerfTest_PKKeyParsing);

#endif

#if defined(BOTAN_HAS_RSA)

class PerfTest_RSAKeyGen final : public PerfTest {
   public:
      void go(const PerfConfig& config) override {
         const auto runtime = config.runtime();
         auto& rng = config.rng();

         for(size_t keylen : {1024, 2048, 3072, 4096}) {
            const std::string nm = Botan::fmt("RSA-{}", keylen);
            auto keygen_timer = config.make_timer(nm, 1, "keygen");

            while(keygen_timer->under(runtime)) {
               auto key =
                  keygen_timer->run([&] { return Botan::create_private_key("RSA", rng, std::to_string(keylen)); });

               BOTAN_ASSERT(key->check_key(rng, true), "Key is ok");
            }

            config.record_result(*keygen_timer);
         }
      }
};

BOTAN_REGISTER_PERF_TEST("RSA_keygen", PerfTest_RSAKeyGen);

#endif

#if defined(BOTAN_HAS_ECDSA)

class PerfTest_ECDSAKeyRec final : public PerfTest {
   public:
      void go(const PerfConfig& config) override {
         const auto runtime = config.runtime();
         auto& rng = config.rng();

         for(const std::string& group_name : config.ecc_groups()) {
            const auto group = Botan::EC_Group::from_name(group_name);
            auto recovery_timer = config.make_timer("ECDSA recovery " + group_name);

            while(recovery_timer->under(runtime)) {
               Botan::ECDSA_PrivateKey key(rng, group);

               std::vector<uint8_t> message(group.get_order_bits() / 8);
               rng.randomize(message.data(), message.size());

               Botan::PK_Signer signer(key, rng, "Raw");
               signer.update(message);
               std::vector<uint8_t> signature = signer.signature(rng);

               Botan::PK_Verifier verifier(key, "Raw", Botan::Signature_Format::Standard, "base");
               verifier.update(message);
               BOTAN_ASSERT(verifier.check_signature(signature), "Valid signature");

               Botan::BigInt r(signature.data(), signature.size() / 2);
               Botan::BigInt s(signature.data() + signature.size() / 2, signature.size() / 2);

               const uint8_t v = key.recovery_param(message, r, s);

               recovery_timer->run([&]() {
                  Botan::ECDSA_PublicKey recovered_key(group, message, r, s, v);
                  BOTAN_ASSERT(recovered_key.public_key_bits() == key.public_key_bits(),
                               "Recovered public key correctly");
               });
            }

            config.record_result(*recovery_timer);
         }
      }
};

BOTAN_REGISTER_PERF_TEST("ecdsa_recovery", PerfTest_ECDSAKeyRec);

#endif

}  // namespace Botan_CLI
