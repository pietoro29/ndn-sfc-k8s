/* producer.cpp */
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/validator-config.hpp>
#include <ndn-cxx/util/io.hpp>
#include <ndn-nac/encryptor.hpp>

#include <fstream>
#include <iostream>

namespace ndn {
namespace nac {
namespace examples {

class Producer
{
public:
  Producer()
    : m_face(nullptr, m_keyChain)
    , m_validator(m_face)
  {
    // --- 1. バリデータの初期化 ---
    m_validator.load(R"CONF(
        trust-anchor
        {
          type any
        }
      )CONF", "fake-config");

    // --- 2. 環境設定の読み込み ---
    const char* prefixEnv = std::getenv("NDN_DATA_PREFIX");
    m_dataPrefix = prefixEnv ? Name(prefixEnv) : Name("/ndn/test");

    const char* kekEnv = std::getenv("NDN_KEK_FILE");
    if (!kekEnv) {
      throw std::runtime_error("NDN_KEK_FILE is not set");
    }

    // --- 3. KEKファイルの読み込み ---
    m_kekData = ndn::io::load<Data>(kekEnv);
    if (!m_kekData) {
      throw std::runtime_error("Failed to load KEK Data from file");
    }
    std::cout << "Loaded KEK: " << m_kekData->getName() << std::endl;

    // --- 4. KEK配信の準備 ---
    Name kekName = m_kekData->getName();
    // Encryptorが投げるInterest (/KEK) を受け取るために、末尾のIDを削って登録する
    Name kekPrefix = kekName.getPrefix(-1);

    std::cout << "Registering Filter for KEK Prefix: " << kekPrefix << std::endl;

    m_kekHandle = m_face.setInterestFilter(
        kekPrefix, // <--- 修正箇所
        [this](const InterestFilter&, const Interest& interest) {
            this->onKekInterest(interest);
        },
        [this](const Name& prefix, const std::string& msg) {
            std::cerr << "Failed to register KEK prefix: " << msg << std::endl;
        }
    );

    // --- 5. Encryptorの初期化 ---
    Name accessPrefix = kekName.getPrefix(-2);
    Name ckPrefix = accessPrefix;

    std::cout << "Initializing Encryptor with AccessPrefix: " << accessPrefix << std::endl;

    m_encryptor = std::make_unique<Encryptor>(
        accessPrefix,
        ckPrefix,
        ndn::security::SigningInfo(m_keyChain.getPib().getDefaultIdentity()),
        // 【修正点】code を int にキャスト
        [](const ErrorCode& code, const std::string& msg) {
            std::cerr << "NAC Error [" << static_cast<int>(code) << "]: " << msg << std::endl;
        },
        m_validator,
        m_keyChain,
        m_face
    );
  }

  void run()
  {
    std::cout << "=== Producer Started ===\n";
    std::cout << "Listening on Data Prefix: " << m_dataPrefix << std::endl;

    m_face.setInterestFilter(
      m_dataPrefix,
      std::bind(&Producer::onContentInterest, this, std::placeholders::_2)
    );

    m_face.processEvents();
  }

private:
  void onContentInterest(const Interest& interest)
  {
    std::cout << "<< Interest for Data received: " << interest.getName() << std::endl;

    std::string msg = "This is a SECRET message from Node1!";

    try {
        auto encryptedContent = m_encryptor->encrypt(
          {reinterpret_cast<const uint8_t*>(msg.data()), msg.size()}
        );

        auto data = std::make_shared<Data>(interest.getName());
        data->setFreshnessPeriod(1_s);
        data->setContent(encryptedContent.wireEncode());

        m_keyChain.sign(*data);

        std::cout << ">> Sending Encrypted Data\n";
        m_face.put(*data);
    }
    catch (const std::exception& e) {
        std::cerr << "Encryption failed (KEK not fetched yet?): " << e.what() << std::endl;
    }
  }

  void onKekInterest(const Interest& interest)
  {
    std::cout << "<< Interest for KEK received: " << interest.getName() << std::endl;
    std::cout << ">> Sending KEK Data\n";
    m_face.put(*m_kekData);
  }

private:
  KeyChain m_keyChain;
  Face m_face;
  ValidatorConfig m_validator;

  std::shared_ptr<Data> m_kekData;
  ScopedRegisteredPrefixHandle m_kekHandle;
  std::unique_ptr<Encryptor> m_encryptor;

  Name m_dataPrefix;
};

} // namespace examples
} // namespace nac
} // namespace ndn

int main(int argc, char** argv)
{
  try {
    ndn::nac::examples::Producer producer;
    producer.run();
    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 1;
  }
}
