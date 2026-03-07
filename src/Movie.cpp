/*
 * Movie: Boost.JSON loading implementation.
 */

#include "Movie.hpp"
#include <boost/json.hpp>
#include <boost/json/value_to.hpp>
#include <boost/system/error_code.hpp>
#include <fstream>
#include <sstream>

namespace json = boost::json;

// Custom conversion: JSON value -> Movie
Movie tag_invoke(const json::value_to_tag<Movie>&, const json::value& jv) {
    const auto& obj = jv.as_object();
    Movie m;
    m.id = static_cast<int>(json::value_to<std::int64_t>(obj.at("id")));
    m.title = json::value_to<std::string>(obj.at("title"));
    m.year = static_cast<int>(json::value_to<std::int64_t>(obj.at("year")));
    m.genre = json::value_to<std::string>(obj.at("genre"));
    m.rating = json::value_to<std::string>(obj.at("rating"));
    m.runtime = static_cast<int>(json::value_to<std::int64_t>(obj.at("runtime")));
    m.synopsis = json::value_to<std::string>(obj.at("synopsis"));
    m.ppv_price = json::value_to<std::string>(obj.at("ppv_price"));
    m.rt_score = static_cast<int>(json::value_to<std::int64_t>(obj.at("rt_score")));
    m.filename = json::value_to<std::string>(obj.at("filename"));
    return m;
}

bool MovieDatabase::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return loadFromString(ss.str());
}

bool MovieDatabase::loadFromString(const std::string& json) {
    boost::system::error_code ec;
    json::value jv = json::parse(json, ec);
    if (ec) {
        return false;
    }
    const auto* obj = jv.if_object();
    if (!obj) {
        return false;
    }
    auto it = obj->find("movies");
    if (it == obj->end() || !it->value().is_array()) {
        return false;
    }
    const auto& arr = it->value().as_array();
    fMovies.clear();
    fMovies.reserve(arr.size());
    try {
        for (const auto& elem : arr) {
            if (elem.is_object()) {
                fMovies.push_back(json::value_to<Movie>(elem));
            }
        }
    } catch (...) {
        fMovies.clear();
        return false;
    }
    return true;
}

const Movie* MovieDatabase::findById(int id) const {
    for (const auto& m : fMovies) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}
