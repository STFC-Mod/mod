// Checks protobuf, zlib and the HTTPS backend as the platform build links them, without the game.
#include <cpr/cpr.h>
#include <curl/curl.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <zlib.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv)
{
  try {
    google::protobuf::Int64Value value;
    value.set_value(42);
    google::protobuf::Int64Value decoded;
    Require(decoded.ParseFromString(value.SerializeAsString()) && decoded.value() == 42, "protobuf wrappers failed");
    google::protobuf::Timestamp timestamp;
    timestamp.set_seconds(123456789);
    google::protobuf::Timestamp restored;
    Require(restored.ParseFromString(timestamp.SerializeAsString()) && restored.seconds() == timestamp.seconds(),
            "protobuf timestamp failed");

    const std::string input = "zig-network-ok";
    unsigned char     compressed[128], output[128];
    uLongf            compressed_size = sizeof(compressed), output_size = sizeof(output);
    Require(compress(compressed, &compressed_size, reinterpret_cast<const Bytef*>(input.data()), input.size()) == Z_OK,
            "zlib compression failed");
    Require(uncompress(output, &output_size, compressed, compressed_size) == Z_OK && output_size == input.size()
                && std::memcmp(output, input.data(), input.size()) == 0,
            "zlib round trip failed");

    const auto* version = curl_version_info(CURLVERSION_NOW);
    Require(version && (version->features & CURL_VERSION_SSL) && (version->features & CURL_VERSION_LIBZ),
            "curl must support TLS and zlib");
#if _WIN32
    Require(version->ssl_version && std::strstr(version->ssl_version, "Schannel"), "Windows must use Schannel");
#else
    Require(version->ssl_version != nullptr, "macOS system curl must provide TLS");
#endif
    if (argc > 1) {
      cpr::Session session;
      session.SetUrl(cpr::Url{argv[1]});
      session.SetTimeout(cpr::Timeout{5000});
      if (argc > 2)
        session.SetSslOptions(cpr::Ssl(cpr::ssl::CaInfo{argv[2]}));
      const auto response = session.Get();
      if (response.error.code != cpr::ErrorCode::OK)
        std::cerr << response.error.message << '\n';
      Require(response.error.code == cpr::ErrorCode::OK && response.status_code == 200 && response.text == input,
              "HTTP(S) round trip failed");
    }
    std::cout << "Dependency smoke checks passed: " << version->ssl_version << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
