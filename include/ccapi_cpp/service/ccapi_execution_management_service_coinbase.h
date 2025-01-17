#ifndef INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_COINBASE_H_
#define INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_COINBASE_H_
#ifdef CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT
#ifdef CCAPI_ENABLE_EXCHANGE_COINBASE
#include "ccapi_cpp/service/ccapi_execution_management_service.h"
namespace ccapi {
class ExecutionManagementServiceCoinbase : public ExecutionManagementService {
 public:
  ExecutionManagementServiceCoinbase(std::function<void(Event& event)> eventHandler, SessionOptions sessionOptions, SessionConfigs sessionConfigs,
                                     ServiceContextPtr serviceContextPtr)
      : ExecutionManagementService(eventHandler, sessionOptions, sessionConfigs, serviceContextPtr) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    this->exchangeName = CCAPI_EXCHANGE_NAME_COINBASE;
    this->baseUrl = sessionConfigs.getUrlWebsocketBase().at(this->exchangeName);
    this->baseUrlRest = this->sessionConfigs.getUrlRestBase().at(this->exchangeName);
    this->setHostFromUrl(this->baseUrlRest);
    this->apiKeyName = CCAPI_COINBASE_API_KEY;
    this->apiSecretName = CCAPI_COINBASE_API_SECRET;
    this->apiPassphraseName = CCAPI_COINBASE_API_PASSPHRASE;
    this->setupCredential({this->apiKeyName, this->apiSecretName, this->apiPassphraseName});
    this->createOrderTarget = "/orders";
    this->cancelOrderTarget = "/orders/<id>";
    this->getOrderTarget = "/orders/<id>";
    this->getOpenOrdersTarget = "/orders";
    this->cancelOpenOrdersTarget = "/orders";
    this->getAccountsTarget = "/accounts";
    this->getAccountBalancesTarget = "/accounts/<account-id>";
    this->orderStatusOpenSet = {"open", "pending", "active"};
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  virtual ~ExecutionManagementServiceCoinbase() {}

 protected:
  void signRequest(http::request<http::string_body>& req, const std::string& body, const std::map<std::string, std::string>& credential) {
    auto apiSecret = mapGetWithDefault(credential, this->apiSecretName);
    auto preSignedText = req.base().at("CB-ACCESS-TIMESTAMP").to_string();
    preSignedText += std::string(req.method_string());
    preSignedText += req.target().to_string();
    preSignedText += body;
    auto signature = UtilAlgorithm::base64Encode(Hmac::hmac(Hmac::ShaVersion::SHA256, UtilAlgorithm::base64Decode(apiSecret), preSignedText));
    req.set("CB-ACCESS-SIGN", signature);
    req.body() = body;
    req.prepare_payload();
  }
  void appendParam(rj::Document& document, rj::Document::AllocatorType& allocator, const std::map<std::string, std::string>& param,
                   const std::map<std::string, std::string> standardizationMap = {}) {
    for (const auto& kv : param) {
      auto key = standardizationMap.find(kv.first) != standardizationMap.end() ? standardizationMap.at(kv.first) : kv.first;
      auto value = kv.second;
      if (key == "side") {
        value = value == CCAPI_EM_ORDER_SIDE_BUY ? "buy" : "sell";
      }
      document.AddMember(rj::Value(key.c_str(), allocator).Move(), rj::Value(value.c_str(), allocator).Move(), allocator);
    }
  }
  void appendSymbolId(rj::Document& document, rj::Document::AllocatorType& allocator, const std::string& symbolId) {
    document.AddMember("product_id", rj::Value(symbolId.c_str(), allocator).Move(), allocator);
  }
  void convertReq(http::request<http::string_body>& req, const Request& request, const TimePoint& now, const std::string& symbolId,
                  const std::map<std::string, std::string>& credential) override {
    req.set(beast::http::field::content_type, "application/json");
    auto apiKey = mapGetWithDefault(credential, this->apiKeyName);
    req.set("CB-ACCESS-KEY", apiKey);
    req.set("CB-ACCESS-TIMESTAMP", std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()));
    auto apiPassphrase = mapGetWithDefault(credential, this->apiPassphraseName, {});
    req.set("CB-ACCESS-PASSPHRASE", apiPassphrase);
    switch (request.getOperation()) {
      case Request::Operation::CREATE_ORDER: {
        req.method(http::verb::post);
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        req.target(this->createOrderTarget);
        rj::Document document;
        document.SetObject();
        rj::Document::AllocatorType& allocator = document.GetAllocator();
        this->appendParam(document, allocator, param,
                          {{CCAPI_EM_ORDER_SIDE, "side"},
                           {CCAPI_EM_ORDER_QUANTITY, "size"},
                           {CCAPI_EM_ORDER_LIMIT_PRICE, "price"},
                           {CCAPI_EM_CLIENT_ORDER_ID, "client_oid"}});
        this->appendSymbolId(document, allocator, symbolId);
        rj::StringBuffer stringBuffer;
        rj::Writer<rj::StringBuffer> writer(stringBuffer);
        document.Accept(writer);
        auto body = stringBuffer.GetString();
        this->signRequest(req, body, credential);
      } break;
      case Request::Operation::CANCEL_ORDER: {
        req.method(http::verb::delete_);
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        std::string id = param.find(CCAPI_EM_ORDER_ID) != param.end()
                             ? param.at(CCAPI_EM_ORDER_ID)
                             : param.find(CCAPI_EM_CLIENT_ORDER_ID) != param.end() ? "client:" + param.at(CCAPI_EM_CLIENT_ORDER_ID) : "";
        auto target = std::regex_replace(this->cancelOrderTarget, std::regex("<id>"), id);
        if (!symbolId.empty()) {
          target += "?product_id=";
          target += symbolId;
        }
        req.target(target);
        this->signRequest(req, "", credential);
      } break;
      case Request::Operation::GET_ORDER: {
        req.method(http::verb::get);
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        std::string id = param.find(CCAPI_EM_ORDER_ID) != param.end()
                             ? param.at(CCAPI_EM_ORDER_ID)
                             : param.find(CCAPI_EM_CLIENT_ORDER_ID) != param.end() ? "client:" + param.at(CCAPI_EM_CLIENT_ORDER_ID) : "";
        auto target = std::regex_replace(this->getOrderTarget, std::regex("<id>"), id);
        req.target(target);
        this->signRequest(req, "", credential);
      } break;
      case Request::Operation::GET_OPEN_ORDERS: {
        req.method(http::verb::get);
        auto target = this->getOpenOrdersTarget;
        if (!symbolId.empty()) {
          target += "?product_id=";
          target += symbolId;
        }
        req.target(target);
        this->signRequest(req, "", credential);
      } break;
      case Request::Operation::CANCEL_OPEN_ORDERS: {
        req.method(http::verb::delete_);
        auto target = this->cancelOpenOrdersTarget;
        if (!symbolId.empty()) {
          target += "?product_id=";
          target += symbolId;
        }
        req.target(target);
        this->signRequest(req, "", credential);
      } break;
      case Request::Operation::GET_ACCOUNTS: {
        req.method(http::verb::get);
        req.target(this->getAccountsTarget);
        this->signRequest(req, "", credential);
      } break;
      case Request::Operation::GET_ACCOUNT_BALANCES: {
        req.method(http::verb::get);
        const std::map<std::string, std::string> param = request.getFirstParamWithDefault();
        auto target = this->getAccountBalancesTarget;
        auto accountId = param.find(CCAPI_EM_ACCOUNT_ID) != param.end() ? param.at(CCAPI_EM_ACCOUNT_ID) : "";
        this->substituteParam(target, {{"<account-id>", accountId}});
        req.target(target);
        this->signRequest(req, "", credential);
      } break;
      default:
        this->convertReqCustom(req, request, now, symbolId, credential);
    }
  }
  std::vector<Element> extractOrderInfoFromRequest(const Request& request, const Request::Operation operation, const rj::Document& document) override {
    const std::map<std::string, std::pair<std::string, JsonDataType> >& extractionFieldNameMap = {
        {CCAPI_EM_ORDER_ID, std::make_pair("id", JsonDataType::STRING)},
        {CCAPI_EM_CLIENT_ORDER_ID, std::make_pair("client_oid", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_SIDE, std::make_pair("side", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_QUANTITY, std::make_pair("size", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_LIMIT_PRICE, std::make_pair("price", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_CUMULATIVE_FILLED_QUANTITY, std::make_pair("filled_size", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_CUMULATIVE_FILLED_PRICE_TIMES_QUANTITY, std::make_pair("executed_value", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_STATUS, std::make_pair("status", JsonDataType::STRING)},
        {CCAPI_EM_ORDER_INSTRUMENT, std::make_pair("product_id", JsonDataType::STRING)}};
    std::vector<Element> elementList;
    if (operation == Request::Operation::CANCEL_ORDER) {
      Element element;
      element.insert(CCAPI_EM_ORDER_ID, document.GetString());
      elementList.emplace_back(std::move(element));
    } else if (operation == Request::Operation::CANCEL_OPEN_ORDERS) {
      for (const auto& x : document.GetArray()) {
        Element element;
        element.insert(CCAPI_EM_ORDER_ID, x.GetString());
        elementList.emplace_back(std::move(element));
      }
    } else {
      if (document.IsObject()) {
        auto element = this->extractOrderInfo(document, extractionFieldNameMap);
        elementList.emplace_back(std::move(element));
      } else {
        for (const auto& x : document.GetArray()) {
          auto element = this->extractOrderInfo(x, extractionFieldNameMap);
          elementList.emplace_back(std::move(element));
        }
      }
    }
    return elementList;
  }
  std::vector<Element> extractAccountInfoFromRequest(const Request& request, const Request::Operation operation, const rj::Document& document) override {
    std::vector<Element> elementList;
    switch (request.getOperation()) {
      case Request::Operation::GET_ACCOUNTS: {
        for (const auto& x : document.GetArray()) {
          Element element;
          element.insert(CCAPI_EM_ACCOUNT_ID, x["id"].GetString());
          elementList.emplace_back(std::move(element));
        }
      } break;
      case Request::Operation::GET_ACCOUNT_BALANCES: {
        Element element;
        element.insert(CCAPI_EM_ACCOUNT_ID, document["id"].GetString());
        element.insert(CCAPI_EM_ASSET, document["currency"].GetString());
        element.insert(CCAPI_EM_QUANTITY_AVAILABLE_FOR_TRADING, document["available"].GetString());
        elementList.emplace_back(std::move(element));
      } break;
      default:
        CCAPI_LOGGER_FATAL(CCAPI_UNSUPPORTED_VALUE);
    }
    return elementList;
  }
  std::vector<std::string> createRequestStringList(const WsConnection& wsConnection, const TimePoint& now,
                                                   const std::map<std::string, std::string>& credential) override {
    auto apiKey = mapGetWithDefault(credential, this->apiKeyName);
    auto apiSecret = mapGetWithDefault(credential, this->apiSecretName);
    auto apiPassphrase = mapGetWithDefault(credential, this->apiPassphraseName);
    auto timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    auto preSignedText = timestamp;
    preSignedText += "GET";
    preSignedText += "/users/self/verify";
    auto signature = UtilAlgorithm::base64Encode(Hmac::hmac(Hmac::ShaVersion::SHA256, UtilAlgorithm::base64Decode(apiSecret), preSignedText));
    std::vector<std::string> requestStringList;
    rj::Document document;
    document.SetObject();
    rj::Document::AllocatorType& allocator = document.GetAllocator();
    document.AddMember("type", rj::Value("subscribe").Move(), allocator);
    rj::Value channels(rj::kArrayType);
    auto subscription = wsConnection.subscriptionList.at(0);
    std::string channelId;
    auto fieldSet = subscription.getFieldSet();
    if (fieldSet.find(CCAPI_EM_ORDER) != fieldSet.end()) {
      channelId = "full";
    } else if (fieldSet.find(CCAPI_EM_PRIVATE_TRADE) != fieldSet.end()) {
      channelId = "matches";
    }
    rj::Value channel(rj::kObjectType);
    rj::Value symbolIds(rj::kArrayType);
    auto instrumentSet = subscription.getInstrumentSet();
    for (const auto& instrument : instrumentSet) {
      auto symbolId = this->convertInstrumentToWebsocketSymbolId(instrument);
      symbolIds.PushBack(rj::Value(symbolId.c_str(), allocator).Move(), allocator);
    }
    channel.AddMember("name", rj::Value(channelId.c_str(), allocator).Move(), allocator);
    channel.AddMember("product_ids", symbolIds, allocator);
    channels.PushBack(channel, allocator);
    rj::Value heartbeatChannel(rj::kObjectType);
    heartbeatChannel.AddMember("name", rj::Value("heartbeat").Move(), allocator);
    rj::Value heartbeatSymbolIds(rj::kArrayType);
    for (const auto& instrument : instrumentSet) {
      auto symbolId = this->convertInstrumentToWebsocketSymbolId(instrument);
      heartbeatSymbolIds.PushBack(rj::Value(symbolId.c_str(), allocator).Move(), allocator);
    }
    heartbeatChannel.AddMember("product_ids", heartbeatSymbolIds, allocator);
    channels.PushBack(heartbeatChannel, allocator);
    document.AddMember("channels", channels, allocator);
    document.AddMember("signature", rj::Value(signature.c_str(), allocator).Move(), allocator);
    document.AddMember("key", rj::Value(apiKey.c_str(), allocator).Move(), allocator);
    document.AddMember("passphrase", rj::Value(apiPassphrase.c_str(), allocator).Move(), allocator);
    document.AddMember("timestamp", rj::Value(timesconvertTextMessageToMessagetamp.c_str(), allocator).Move(), allocator);
    rj::StringBuffer stringBuffer;
    rj::Writer<rj::StringBuffer> writer(stringBuffer);
    document.Accept(writer);
    std::string requestString = stringBuffer.GetString();
    requestStringList.push_back(requestString);
    return requestStringList;
  }
  std::string apiPassphraseName;
#ifdef GTEST_INCLUDE_GTEST_GTEST_H_

 public:
  using ExecutionManagementService::convertRequest;
  using ExecutionManagementService::convertTextMessageToMessageRest;
  FRIEND_TEST(ExecutionManagementServiceCoinbaseTest, signRequest);
#endif
};
} /* namespace ccapi */
#endif
#endif
#endif  // INCLUDE_CCAPI_CPP_SERVICE_CCAPI_EXECUTION_MANAGEMENT_SERVICE_COINBASE_H_
