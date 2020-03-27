#ifndef TGBOT_CURLHTTPCLIENT_H
#define TGBOT_CURLHTTPCLIENT_H

#include "tgbot/net/HttpClient.h"
#include "tgbot/net/Url.h"

#include <string>
#include <vector>

// fwd decl
typedef void CURL;

namespace TgBot {

/**
 * @brief This class makes http requests via libcurl.
 *
 * @ingroup net
 */
class TGBOT_API CurlHttpClient : public HttpClient {

public:
    CurlHttpClient();
    ~CurlHttpClient() override;

    /**
     * @brief Sends a request to the url.
     *
     * If there's no args specified, a GET request will be sent, otherwise a POST request will be sent.
     * If at least 1 arg is marked as file, the content type of a request will be multipart/form-data, otherwise it will be application/x-www-form-urlencoded.
     */
    std::string makeRequest(const Url& url, const std::string &json) const override;

    /**
     * @brief Raw curl settings storage for fine tuning.
     */
    CURL* curlSettings;
};

}

#endif //TGBOT_CURLHTTPCLIENT_H
