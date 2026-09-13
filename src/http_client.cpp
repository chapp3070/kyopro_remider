#include "atcoder_reminder/http_client.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace reminder {
namespace {

struct TransferContext {
    std::string body;
    std::size_t maxBytes{};
    bool tooLarge{false};
    std::vector<std::pair<std::string, std::string>> headers;
};

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::size_t writeCallback(char* data, std::size_t size, std::size_t count, void* user) {
    auto* context = static_cast<TransferContext*>(user);
    const std::size_t bytes = size * count;
    if (bytes > context->maxBytes - std::min(context->maxBytes, context->body.size())) {
        context->tooLarge = true;
        return 0;
    }
    context->body.append(data, bytes);
    return bytes;
}

std::size_t headerCallback(char* data, std::size_t size, std::size_t count, void* user) {
    auto* context = static_cast<TransferContext*>(user);
    const std::size_t bytes = size * count;
    std::string line(data, bytes);
    const std::size_t separator = line.find(':');
    if (separator != std::string::npos) {
        context->headers.emplace_back(
            lower(trim(line.substr(0, separator))),
            trim(line.substr(separator + 1)));
    }
    return bytes;
}

HttpResponse perform(const Config& config,
                     const std::string& url,
                     const std::vector<std::string>& headers,
                     bool post,
                     const std::string& body,
                     std::size_t maxBytes,
                     const std::string& allowedPrefix) {
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = "curl_easy_init failed";
        return response;
    }

    TransferContext context;
    context.maxBytes = maxBytes;
    struct curl_slist* requestHeaders = nullptr;
    for (const std::string& header : headers) {
        requestHeaders = curl_slist_append(requestHeaders, header.c_str());
    }

    char errorBuffer[CURL_ERROR_SIZE]{};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // The Discord API endpoint must not silently turn a POST into a request
    // to another host. AtCoder may redirect its public contest page, so only
    // GET requests follow redirects.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, post ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config.requestTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config.requestTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "atcoder-abc-reminder/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &context);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, requestHeaders);
    if (post) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        response.error = context.tooLarge
            ? "HTTP response exceeded configured size limit"
            : (errorBuffer[0] != '\0' ? errorBuffer : curl_easy_strerror(result));
    } else {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        char* effectiveUrl = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
        if (effectiveUrl == nullptr || std::string(effectiveUrl).rfind(allowedPrefix, 0) != 0) {
            response.error = "unexpected redirect destination";
        } else {
            response.status = status;
            response.body = std::move(context.body);
            response.headers = std::move(context.headers);
        }
    }

    if (requestHeaders != nullptr) {
        curl_slist_free_all(requestHeaders);
    }
    curl_easy_cleanup(curl);
    return response;
}

} // namespace

CurlHttpClient::CurlHttpClient(const Config& config) : config_(config) {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

CurlHttpClient::~CurlHttpClient() = default;

HttpResponse CurlHttpClient::get(const std::string& url, std::size_t maxBytes) {
    return perform(config_, url, {"Accept: text/html"}, false, {}, maxBytes,
                   "https://atcoder.jp/");
}

HttpResponse CurlHttpClient::getJson(const std::string& url,
                                     const std::string& authorization,
                                     std::size_t maxBytes) {
    return perform(config_, url,
                   {"Accept: application/json", "Authorization: " + authorization},
                   false, {}, maxBytes, "https://discord.com/");
}

HttpResponse CurlHttpClient::postJson(const std::string& url,
                                      const std::string& authorization,
                                      const std::string& body) {
    return perform(config_, url,
                   {"Accept: application/json", "Content-Type: application/json",
                    "Authorization: " + authorization},
                   true, body, 512U * 1024U, "https://discord.com/");
}

} // namespace reminder
