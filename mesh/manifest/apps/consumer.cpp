/* consumer.cpp */
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/validator-config.hpp>
#include <ndn-cxx/util/io.hpp> // 追加: ファイル読み込み用
#include <ndn-nac/decryptor.hpp>

#include <iostream>
#include <cstdlib>

namespace ndn {
namespace nac {
namespace examples {

class Consumer
{
public:
  Consumer()
    : m_face(nullptr, m_keyChain)
    , m_validator(m_face)
    , m_decryptor(
        m_keyChain.getPib().getDefaultIdentity().getDefaultKey(), // Consumer Key
        m_validator,
        m_keyChain,
        m_face)
  {
    // --- 1. バリデータの初期化 ---
    m_validator.load(R"CONF(
        trust-anchor
        {
          type any
        }
      )CONF", "fake-config");

    // --- 2. 環境設定 ---
    const char* env_data = std::getenv("NDN_DATA_PREFIX");
    m_dataPrefix = env_data ? Name(env_data) : Name("/ndn/test");

    // --- 3. KDK (復号許可証) の読み込みと自己配信 ---
    // Decryptorは復号時にKDKをネットワーク(NFD)に問い合わせます。
    // ここでファイルを読み込み、その問い合わせに答えられるようにしておきます。
    const char* kdkEnv = std::getenv("NDN_KDK_FILE");
    if (kdkEnv) {
        m_kdkData = ndn::io::load<Data>(kdkEnv);
        if (m_kdkData) {
            std::cout << "Loaded KDK: " << m_kdkData->getName() << std::endl;

            // KDKの要求が来たら即座に答える
            m_kdkHandle = m_face.setInterestFilter(
                m_kdkData->getName(),
                [this](const InterestFilter&, const Interest& interest) {
                    // std::cout << ">> Serving KDK for self" << std::endl;
                    m_face.put(*m_kdkData);
                },
                [](const Name&, const std::string& msg) {
                    std::cerr << "Failed to register KDK prefix: " << msg << std::endl;
                }
            );
        } else {
            std::cerr << "WARNING: Failed to load KDK from " << kdkEnv << std::endl;
        }
    } else {
        std::cerr << "WARNING: NDN_KDK_FILE is not set. Decryption may fail if KDK is not in Repo." << std::endl;
    }
  }

  void run()
  {
    Interest interest(m_dataPrefix);
    interest.setCanBePrefix(true);
    interest.setMustBeFresh(true);
    interest.setInterestLifetime(3_s);

    std::cout << "=== Consumer Started ===" << std::endl;
    std::cout << "Sending Interest for: " << interest << std::endl;

    m_face.expressInterest(
      interest,
      std::bind(&Consumer::onData, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&Consumer::onNack, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&Consumer::onTimeout, this, std::placeholders::_1));

    m_face.processEvents();
  }

private:
  void onData(const Interest&, const Data& data)
  {
    std::cout << "Received Data. Validating and Decrypting..." << std::endl;

    m_validator.validate(
      data,
      [this] (const Data& data) {
        std::cout << "Validation successful. Decrypting content..." << std::endl;

        const Block& encrypted = data.getContent();

        m_decryptor.decrypt(
          encrypted,
          // 成功時
          [] (const ConstBufferPtr& content) {
            std::cout << "\n****************************************\n"
                      << "SUCCESS! Decrypted content:\n"
                      << std::string(reinterpret_cast<const char*>(content->data()), content->size())
                      << "\n****************************************\n" << std::endl;
            exit(0);
          },
          // 失敗時
          [] (const ErrorCode& code, const std::string& error) {
            // 【修正箇所】 code を int にキャスト
            std::cerr << "FAILURE: Cannot decrypt data.\n"
                      << "Code: " << static_cast<int>(code) << "\n"
                      << "Error: " << error << std::endl;
            exit(1);
          });
      },
      // バリデーション失敗時
      [] (const Data&, const ValidationError& error) {
        std::cerr << "FAILURE: Validation error: " << error << std::endl;
        exit(1);
      });
  }

  void onNack(const Interest&, const lp::Nack& nack) const
  {
    std::cout << "Received Nack: " << nack.getReason() << std::endl;
    exit(1);
  }

  void onTimeout(const Interest&) const
  {
    std::cout << "Timeout (Data not received)." << std::endl;
    exit(1);
  }

private:
  KeyChain m_keyChain;
  Face m_face;
  ValidatorConfig m_validator;
  Decryptor m_decryptor;

  // KDK保持用
  std::shared_ptr<Data> m_kdkData;
  ScopedRegisteredPrefixHandle m_kdkHandle;

  Name m_dataPrefix;
};

} // namespace examples
} // namespace nac
} // namespace ndn

int main(int argc, char** argv)
{
  try {
    ndn::nac::examples::Consumer consumer;
    consumer.run();
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
