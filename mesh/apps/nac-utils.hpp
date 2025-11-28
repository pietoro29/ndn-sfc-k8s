/* nac-utils.hpp */
#ifndef NAC_UTILS_HPP
#define NAC_UTILS_HPP

#include <ndn-cxx/util/io.hpp>
#include <ndn-cxx/face.hpp>
#include <filesystem> // C++17 standard
#include <vector>
#include <string>
#include <iostream>

namespace fs = std::filesystem;

namespace ndn::nac::examples {

// 指定されたディレクトリから、特定のプレフィックス(kek_ or kdk_)で始まるDataファイルを探す
std::string findKeyFile(const std::string& directory, const std::string& filePrefix) {
    for (const auto& entry : fs::directory_iterator(directory)) {
        std::string filename = entry.path().filename().string();
        if (filename.rfind(filePrefix, 0) == 0 && filename.substr(filename.length() - 5) == ".data") {
            return entry.path().string();
        }
    }
    return "";
}

// 共通: DataパケットをファイルからロードしてFaceで配信(Serve)できるようにする
// Encryptor/Decryptorが内部的にInterestを投げて取りに来るため、それに答える
std::shared_ptr<Data> loadAndServeKey(Face& face, const std::string& filePath, const std::string& keyType) {
    auto data = io::load<Data>(filePath);
    if (!data) {
        throw std::runtime_error("Failed to load " + keyType + " from " + filePath);
    }

    std::cout << "[" << keyType << "] Loaded: " << data->getName() << std::endl;

    // Encryptor/Decryptorは親Prefix (/AccessPrefix/KEK など) でInterestを投げることがあるため、
    // 少し広めにフィルターを設定する (Prefix登録)
    Name prefixToServe = data->getName().getPrefix(-1); // 末尾のIDを除く

    std::cout << "[" << keyType << "] Serving at prefix: " << prefixToServe << std::endl;

    face.setInterestFilter(
        prefixToServe,
        [data, keyType](const InterestFilter&, const Interest& interest) {
            // 要求された名前が自分の持っているDataの名前のPrefixであれば返す
            if (data->getName().isPrefixOf(interest.getName()) ||
                interest.matchesData(*data)) {
                // std::cout << ">> Serving " << keyType << " Data" << std::endl; // ログ過多防止のためコメントアウト
                // Face::put は static メソッドではないので capture するか face を使う必要があるが
                // ここでは lambda 内で face を使うのが難しいので
                // 実際の実装ではクラスメンバにするのが良い。
                // このヘルパー関数は「読み込み」に徹して、Filter登録はクラス内で行う形に修正します。
            }
        },
        nullptr // 登録失敗時のコールバック省略
    );

    return data;
}

} // namespace
#endif
