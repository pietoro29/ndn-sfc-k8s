/* producer.cpp */
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/validator-config.hpp>
#include <ndn-cxx/util/io.hpp>
#include <ndn-nac/encryptor.hpp>

#include "nac-utils.hpp"

#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace ndn::nac::examples {

class Producer
{
public:
  Producer()
    : m_face(nullptr, m_keyChain)
    , m_validator(m_face)
  {
    // 1. 環境設定
    const char* prefixEnv = std::getenv("NDN_DATA_PREFIX");
    m_dataPrefix = prefixEnv ? Name(prefixEnv) : Name("/ndn/test/data");

    // バリデータ (テスト用: 全許可)
    m_validator.load(R"CONF(trust-anchor { type any })CONF", "fake-config");

    // 2. KEKの自動検知とロード
    // 第2引数に "kek_" を指定することで、KEKファイルを探させる
    std::string kekPath = findKeyFile("/data/nac-data", "kek_");

    if (kekPath.empty()) {
        throw std::runtime_error("No KEK file (kek_*.data) found in /data/nac-data/");
    }

    m_kekData = ndn::io::load<Data>(kekPath);
    if (!m_kekData) throw std::runtime_error("Failed to parse KEK data");

    std::cout << "Loaded KEK: " << m_kekData->getName() << std::endl;

    // 3. KEK配信の登録 (Self-Serving)
    m_kekHandle = m_face.setInterestFilter(
        m_kekData->getName().getPrefix(-1), // /ndn/AM/.../KEK で待ち受け
        [this](const InterestFilter&, const Interest& interest) {
             m_face.put(*m_kekData);
        },
        [](const Name&, const std::string& msg) {
            std::cerr << "Failed to register KEK prefix: " << msg << std::endl;
        }
    );

    // 4. Encryptor 初期化
    Name accessPrefix = m_kekData->getName().getPrefix(-2); // KEK/<id> を除く
    m_encryptor = std::make_unique<Encryptor>(
        accessPrefix,
        accessPrefix, // CK Prefix
        ndn::security::SigningInfo(m_keyChain.getPib().getDefaultIdentity()),
        [](const ErrorCode& code, const std::string& msg) {
            std::cerr << "NAC Error: " << msg << std::endl;
        },
        m_validator,
        m_keyChain,
        m_face
    );
  }

  void run()
  {
    std::cout << "=== Producer Ready ===" << std::endl;
    std::cout << "Data Prefix: " << m_dataPrefix << std::endl;

    //アプリケーション側でのデータ署名検証のために、identity certを配信
    try {
        std::string certPath = "/data/nac-data/self.cert";

        if (fs::exists(certPath)) {
            auto cert = ndn::io::load<Data>(certPath);
            if (cert) {
                std::cout << "Loaded Certificate: " << cert->getName() << std::endl;

                Name prefixToServe = cert->getName().getPrefix(-2);
                std::cout << "Serving Certificate at prefix: " << prefixToServe << std::endl;

                // 証明書の配信登録
                m_face.setInterestFilter(
                    prefixToServe,
                    [cert, this](const InterestFilter&, const Interest& interest) {
                        // 【修正ポイント2】 シンプルに matchesData で判定
                        if (interest.matchesData(*cert)) {
                             // std::cout << ">> Serving Certificate" << std::endl;
                             m_face.put(*cert);
                        }
                    },
                    [](const Name&, const std::string& msg) {
                        std::cerr << "Failed to register Cert prefix: " << msg << std::endl;
                    }
                );
            } else {
                std::cerr << "WARN: Failed to parse self.cert" << std::endl;
            }
        } else {
            std::cerr << "WARN: self.cert not found in /data/nac-data/" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "WARN: Error loading certificate: " << e.what() << std::endl;
    }


    m_face.setInterestFilter(
        InterestFilter(m_dataPrefix),
        std::bind(&Producer::onContentInterest, this, std::placeholders::_2),
        // Success callback (only prefix!)
        [](const Name& prefix) {
            std::cout << ">>> SUCCESS: Registered prefix " << prefix << std::endl;
        },

        // Failure callback (prefix + reason)
        [](const Name& prefix, const std::string& reason) {
            std::cerr << ">>> ERROR: Failed to register prefix "
                      << prefix
                      << ". Reason: " << reason
                      << std::endl;
        }
    );

    m_face.processEvents();
  }

private:
  void onContentInterest(const Interest& interest) {
    std::cout << "<< Interest: " << interest.getName() << std::endl;
    std::string content = "Secure Data created at " + std::to_string(std::time(nullptr));

    try {
        auto encrypted = m_encryptor->encrypt({reinterpret_cast<const uint8_t*>(content.data()), content.size()});
        auto data = std::make_shared<Data>(interest.getName());
        data->setFreshnessPeriod(1_s);
        data->setContent(encrypted.wireEncode());
        m_keyChain.sign(*data);
        m_face.put(*data);
        std::cout << ">> Sent Encrypted Data" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Encryption Error: " << e.what() << std::endl;
    }
  }

  KeyChain m_keyChain;
  Face m_face;
  ValidatorConfig m_validator;
  std::shared_ptr<Data> m_kekData;
  ScopedRegisteredPrefixHandle m_kekHandle;
  std::unique_ptr<Encryptor> m_encryptor;
  Name m_dataPrefix;
};

} // namespace

int main() {
  try {
    ndn::nac::examples::Producer producer;
    producer.run();
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
}
