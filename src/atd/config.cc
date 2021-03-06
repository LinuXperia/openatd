/* Copyright 2017 Paolo Galeone <nessuno@nerdz.eu>. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.*/

#include <atd/config.hpp>

namespace atd {

// Stupid but fast hash function
inline constexpr int _hash(const char* value)
{
    if (value == NULL) {
        return 0;
    }
    int ret = 0, i = 0;
    while (value[i] != '\0') {
        ret ^= value[i] << (i % 8);
        i++;
    }
    return ret;
}

Config::Config(const std::string& path)
{
    std::ifstream ifconf(path);
    ifconf >> _config;
}

quantity_t quantity_from_json(json obj)
{
    quantity_t ret = {};
    bool error = false;
    try {
        ret.fixed_amount = obj.at("fixed_amount").get<float>();
    }
    catch (const json::out_of_range&) {
        error = true;
    }

    try {
        ret.balance_percentage = obj.at("balance_percentage").get<float>();
    }
    catch (const json::out_of_range&) {
        if (error) {
            throw std::runtime_error(
                obj.dump() +
                ": fixed_amount or "
                "balance_percentage required in configuration");
        }
    }
    return ret;
}

// markets returns a map of initialized markets from the configuration file
std::map<std::string, std::shared_ptr<Market>> Config::markets()
{
    std::map<std::string, std::shared_ptr<Market>> ret;
    json markets = _config["markets"];

    for (json::iterator it = markets.begin(); it != markets.end(); ++it) {
        switch (_hash(it.key().c_str())) {
            case _hash("kraken"): {
                auto value = *it;
                ret.insert(std::pair(
                    "kraken", std::make_shared<Kraken>(value["apiKey"],
                                                       value["apiSecret"])));
                std::cout << "Market [Kraken]: initialized\n";
            } break;
            default:
                std::stringstream ss;
                ss << it.key() << " is not a valid key";
                throw std::runtime_error(ss.str());
        }
    }

    return ret;
}

// exchanges returns a map of initialized exchanges from the configuration
// file
std::map<std::string, std::shared_ptr<Exchange>> Config::exchanges()
{
    std::map<std::string, std::shared_ptr<Exchange>> ret;
    json exchanges = _config["exchanges"];

    for (json::iterator it = exchanges.begin(); it != exchanges.end(); ++it) {
        switch (_hash(it.key().c_str())) {
            case _hash("shapeshift"): {
                auto value = *it;
                if (value.find("affiliatePrivateKey") != value.end()) {
                    ret.insert(std::pair(
                        "shapeshift",
                        std::make_shared<Shapeshift>(
                            value["affiliatePrivateKey"].get<std::string>())));
                }
            } break;
            default:
                std::stringstream ss;
                ss << it.key() << " is not a valid key";
                throw std::runtime_error(ss.str());
        }
    }

    // Shapeshift is always available because it does not require any
    // credentials
    if (ret.find("shapeshift") == ret.end()) {
        ret.insert(std::pair("shapeshift", std::make_shared<Shapeshift>()));
    }
    std::cout << "Exchange [ShapeShift]: initialized\n";
    return ret;
}

std::chrono::seconds Config::monitorPeriod()
{
    return std::chrono::seconds(_config["monitor"]["period"]);
}

std::vector<currency_pair_t> Config::monitorPairs()
{
    std::vector<currency_pair_t> pairs;
    for (const auto& pair : _config["monitor"]["pairs"]) {
        pairs.push_back(currency_pair_t(pair.at(0), pair.at(1)));
    }
    return pairs;
}

std::vector<std::string> Config::monitorCurrencies()
{
    return _config["monitor"]["currencies"];
}

std::map<currency_pair_t, std::vector<std::shared_ptr<Strategy>>>
Config::strategies(std::shared_ptr<DataMonitor> monitors,
                   std::shared_ptr<channel<atd::message_t>> chan)
{
    std::map<currency_pair_t, std::vector<std::shared_ptr<Strategy>>> ret;

    json strategies = _config["strategies"];
    for (json::iterator it = strategies.begin(); it != strategies.end(); ++it) {
        std::string base = it.key();
        json object = it.value();
        for (json::iterator quote_it = object.begin(); quote_it != object.end();
             ++quote_it) {
            std::string quote = quote_it.key();
            auto strategy_array = quote_it.value();
            for (const auto strategy_obj : strategy_array) {
                std::string name = strategy_obj["name"];
                at::toupper(name);

                auto pair = currency_pair_t(base, quote);
                switch (_hash(name.c_str())) {
                    case _hash("HODL"): {
                        ret[pair].push_back(
                            std::make_shared<Hodl>(monitors, chan));
                        std::cout << pair << ": strategy HODL\n";
                        break;
                    }
                    case _hash("BUYLOWANDHODL"): {
                        auto params = strategy_obj["params"];
                        float low = params["low"];
                        auto trade_period =
                            std::chrono::seconds(params["trade_period"]);
                        auto stats_period =
                            std::chrono::seconds(params["stats_period"]);
                        auto balance_percentage =
                            params["quote"]["balance_percentage"].get<float>();
                        ret[pair].push_back(std::make_shared<BuyLowAndHodl>(
                            monitors, chan, low, balance_percentage,
                            trade_period, stats_period));
                        std::cout << pair << ": strategy BuyLowAndHodl\n";
                        break;
                    }
                    case _hash("DOLLARCOSTAVERAGING"): {
                        auto params = strategy_obj["params"];
                        atd::quantity_t buy_quantity =
                            quantity_from_json(params["buy"]["quote"]);
                        std::string date = params["date"];

                        ret[pair].push_back(
                            std::make_shared<DollarCostAveraging>(
                                monitors, chan, date, buy_quantity));
                        std::cout << pair << ": strategy DollarCostAveraging\n";
                        break;
                    }
                    case _hash("SMALLCHANGES"): {
                        auto params = strategy_obj["params"];
                        quantity_t buy_quantity =
                            quantity_from_json(params["buy"]["base"]);
                        quantity_t sell_quantity =
                            quantity_from_json(params["sell"]["base"]);

                        ret[pair].push_back(std::make_shared<SmallChanges>(
                            monitors, chan, buy_quantity, sell_quantity));
                        std::cout << pair << ": strategy SmallChanges\n";
                        break;
                    }
                        /*
        case _hash("BUYLOWSELLHIGH"): {
        auto params = strategy_obj["params"];
        float low = params["low"], high = params["high"];
        auto trade_period = std::chrono::seconds(params["trade_period"]);
        auto stats_period =
        std::chrono::seconds(params["stats_period"]);
        auto balance_percentage = params["balance_percentage"].get<float>();
        ret[pair].push_back(
        std::make_shared<BuyLowSellHigh>(monitors, chan,
                                    low, high, balance_percentage, trade_period,
        stats_period));
        std::cout << pair << ": strategy BuyLowSellHigh\n";
        break;
        }*/
                    default:
                        std::stringstream ss;
                        ss << name << " is not a valid key";
                        throw std::runtime_error(ss.str());
                }
            }
        }
    }
    return ret;
}  // namespace atd

}  // end namespace atd
