/* consumer.cpp (KDKネットワーク取得対応版) */
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/security/validator-config.hpp>
#include <ndn-cxx/util/io.hpp>
#include <ndn-nac/decryptor.hpp>
#include <ndn-cxx/security/pib/identity.hpp>
#include "nac-utils.hpp"

#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

namespace ndn::nac::examples {

class Consumer
{
public:
  Consumer()
    : m_face(nullptr, m_keyChain)
    , m_validator(m_face)
  {
    // 1. 環境設定
    const char* prefixEnv = std::getenv("NDN_DATA_PREFIX");
    m_dataPrefix = prefixEnv ? Name(prefixEnv) : Name("/ndn/test/data");

    // バリデータ設定 (テスト用: 全許可)
    m_validator.load(R"CONF(trust-anchor { type any })CONF", "fake-config");
  }

  void run()
  {
    // 2. KDKの検索 (ローカル -> なければネットワーク)
    std::string kdkPath = findKeyFile("/data/nac-data", "kdk_");

    if (!kdkPath.empty()) {
        std::cout << "Found local KDK: " << kdkPath << std::endl;
        auto kdkData = ndn::io::load<Data>(kdkPath);
        if (kdkData) {
            initializeDecryptor(kdkData);
            sendContentInterest(); // 準備完了なのでコンテンツを取りに行く
        } else {
            std::cerr << "Failed to load local KDK. Trying network..." << std::endl;
            fetchKdk();
        }
    } else {
        std::cout << "No local KDK found. Fetching from KDK Server..." << std::endl;
        fetchKdk();
    }

    m_face.processEvents();
  }

private:
  // KDKをネットワークから取得する
  void fetchKdk() {
      // KDKの名前は通常 /<DataPrefix>/NAC/KDK/<KEK-ID>/... となる
      // 正確な名前がわからないため、Prefix探索を行う
      Name kdkQuery = m_dataPrefix;
      kdkQuery.append("NAC").append("KDK");

      Interest interest(kdkQuery);
      interest.setCanBePrefix(true); // 具体的なIDが不明なためPrefixで検索
      interest.setMustBeFresh(true);

      std::cout << "=== Fetching KDK: " << interest.getName() << " ===" << std::endl;

      m_face.expressInterest(interest,
          std::bind(&Consumer::onKdkData, this, std::placeholders::_1, std::placeholders::_2),
          std::bind(&Consumer::onNack, this, std::placeholders::_1, std::placeholders::_2),
          std::bind(&Consumer::onTimeout, this, std::placeholders::_1)
      );
  }

  // KDK受信時の処理
  void onKdkData(const Interest&, const Data& data) {
      std::cout << "Received KDK Data: " << data.getName() << std::endl;
      // メモリ上のDataをshared_ptrとしてコピー
      auto kdkData = std::make_shared<Data>(data);

      try {
          initializeDecryptor(kdkData);
          // KDKの準備ができたら、本来のコンテンツを取りに行く
          sendContentInterest();
      } catch (const std::exception& e) {
          std::cerr << "Failed to initialize Decryptor with fetched KDK: " << e.what() << std::endl;
          exit(1);
      }
  }

  // Decryptorの初期化 (ローカル/ネットワーク共通)
  void initializeDecryptor(std::shared_ptr<Data> kdkData) {
    m_kdkData = kdkData;

    // Node3のIdentityを特定する (setup.shで生成されたもの)
    // ※今回はハードコードだが、汎用化するなら環境変数等で自身のIDを渡す
    Name identityName("/ndn/waseda/labA/ndn-node3");

    ndn::security::pib::Identity myIdentity;
    try {
        myIdentity = m_keyChain.getPib().getIdentity(identityName);
    } catch (const std::exception&) {
        std::cout << "WARN: Full identity " << identityName << " not found. Using default." << std::endl;
        myIdentity = m_keyChain.getPib().getDefaultIdentity();
    }

    std::cout << "Initializing Decryptor with Identity: " << myIdentity.getName() << std::endl;

    // Decryptor作成
    m_decryptor = std::make_unique<Decryptor>(
        myIdentity.getDefaultKey(),
        m_validator,
        m_keyChain,
        m_face
    );

    // KDK配信登録 (Decryptorが内部でKDKを要求したときに答えるため)
    // 名前構造: .../ENCRYPTED-BY/<KeyLocator>
    // ENCRYPTED-BY コンポーネントを探す
    Name kdkName = m_kdkData->getName();
    ssize_t encryptedByIdx = -1;
    for (size_t i = 0; i < kdkName.size(); ++i) {
        if (kdkName[i].toUri() == "ENCRYPTED-BY") {
            encryptedByIdx = i;
            break;
        }
    }

    if (encryptedByIdx != -1) {
        Name prefixToServe = m_kdkData->getName().getPrefix(encryptedByIdx + 1);
        std::cout << "Serving KDK to Decryptor at: " << prefixToServe << std::endl;

        m_kdkHandle = m_face.setInterestFilter(
            prefixToServe,
            [this](const InterestFilter&, const Interest& interest) {
                if (interest.matchesData(*m_kdkData)) {
                     m_face.put(*m_kdkData);
                }
            },
            [](const Name&, const std::string& msg) { std::cerr << "KDK Reg Failed: " << msg << std::endl; }
        );
    }
  }

  // コンテンツ取得リクエスト
  void sendContentInterest() {
    Interest interest(m_dataPrefix);
    interest.setCanBePrefix(true);
    interest.setMustBeFresh(true);
    std::cout << "=== Consumer Sending Interest: " << interest << " ===" << std::endl;

    m_face.expressInterest(interest,
      std::bind(&Consumer::onData, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&Consumer::onNack, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&Consumer::onTimeout, this, std::placeholders::_1)
    );
  }

  void onData(const Interest&, const Data& data) {
    std::cout << "Received Content Data. Decrypting..." << std::endl;

    if (!m_decryptor) {
        std::cerr << "FATAL: Decryptor not initialized!" << std::endl;
        exit(1);
    }

    Block contentBlock = data.getContent();
    contentBlock.parse();
    Block encryptedContent = contentBlock.blockFromValue();

    m_decryptor->decrypt(encryptedContent,
        [](const ConstBufferPtr& c) {
            std::cout << "\n*** SUCCESS! Decrypted: "
                      << std::string(reinterpret_cast<const char*>(c->data()), c->size())
                      << " ***\n" << std::endl;
            exit(0);
        },
        [](const ErrorCode& c, const std::string& e) {
            std::cerr << "Decryption Failed [" << static_cast<int>(c) << "]: " << e << std::endl;
            exit(1);
        }
    );
  }

  void onNack(const Interest& interest, const lp::Nack& nack) const {
    std::cerr << "Nack for " << interest.getName() << ": " << nack.getReason() << std::endl;
    exit(1);
  }

  void onTimeout(const Interest& interest) const {
    std::cerr << "Timeout for " << interest.getName() << std::endl;
    exit(1);
  }

  KeyChain m_keyChain;
  Face m_face;
  ValidatorConfig m_validator;
  std::unique_ptr<Decryptor> m_decryptor;
  std::shared_ptr<Data> m_kdkData;
  ScopedRegisteredPrefixHandle m_kdkHandle;
  Name m_dataPrefix;
};

} // namespace

int main() {
    try {
        ndn::nac::examples::Consumer c;
        c.run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
