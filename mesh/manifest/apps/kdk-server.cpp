#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>
#include <ndn-cxx/util/io.hpp>
#include <iostream>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace ndn::nac::server {

class KdkServer
{
public:
  KdkServer()
    : m_face(nullptr, m_keyChain)
  {
    loadDataFiles("/data/nac-data");
  }

  void run()
  {
    if (m_store.empty()) {
      std::cerr << "WARN: No data loaded. Server is idle." << std::endl;
    } else {
      std::cout << "KDK Server running. Serving " << m_store.size() << " data packets." << std::endl;
    }
    m_face.processEvents();
  }

private:
  void loadDataFiles(const std::string& pathStr)
  {
    fs::path dir(pathStr);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
      std::cerr << "Error: Directory not found: " << pathStr << std::endl;
      return;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.path().extension() == ".data") {
        try {
          auto data = ndn::io::load<Data>(entry.path().string());
          if (data) {
            storeData(data);
          }
        }
        catch (const std::exception& e) {
          std::cerr << "Failed to load " << entry.path() << ": " << e.what() << std::endl;
        }
      }
    }
  }

  void storeData(std::shared_ptr<Data> data)
  {
    Name name = data->getName();
    m_store[name] = data;

    std::cout << "Loaded: " << name << std::endl;

    // NACのKDKやKEKは、名前が特殊であるため、
    // ここでは単純化のために「Dataの完全名」でInterestFilterを設定するのではなく、
    // 「AccessManagerのPrefix」や「KEKのPrefix」を広報するのが正しい。
    // しかし、動的にPrefixを決定するのが難しいため、
    // 「Dataの名前そのもの(Full Name without implicit digest)」に対してFilterを設定するアプローチをとる。
    // ※あるいは、AMのPrefix ("/ndn/AM") が固定なら、コンストラクタで1回だけ登録する方が効率的。

    // 今回は確実にHitさせるため、各データの名前で登録する (データ数が少ない実験環境ならこれでOK)
    m_face.setInterestFilter(name,
      [this, data](const InterestFilter&, const Interest& interest) {
        // InterestがCanBePrefixを持っている場合などに対応
        if (data->getName().isPrefixOf(interest.getName()) ||
            interest.matchesData(*data)) {
          std::cout << "Serving: " << data->getName() << std::endl;
          m_face.put(*data);
        }
      },
      [](const Name& prefix, const std::string& msg) {
        std::cerr << "Register failed for " << prefix << ": " << msg << std::endl;
      }
    );
  }

private:
  KeyChain m_keyChain;
  Face m_face;
  // メモリ上にDataを保持する簡易ストア
  std::map<Name, std::shared_ptr<Data>> m_store;
};

} // namespace

int main(int argc, char** argv)
{
  try {
    ndn::nac::server::KdkServer server;
    server.run();
  }
  catch (const std::exception& e) {
    std::cerr << "FATAL: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
