/*
 * Movie: Data structure and database for movie catalog loaded from JSON.
 * Uses Boost.JSON for parsing. Intended for rendering (e.g., poster filenames, titles).
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Movie {
    int id = 0;
    std::string title;
    int year = 0;
    std::string genre;
    std::string rating;
    int runtime = 0;
    std::string synopsis;
    std::string ppv_price;
    int rt_score = 0;
    std::string filename;
};

class MovieDatabase {
public:
    MovieDatabase() = default;

    /** Load movies from a JSON file. Returns true on success. */
    bool loadFromFile(const std::string& path);

    /** Load movies from a JSON string. Returns true on success. */
    bool loadFromString(const std::string& json);

    /** Number of movies in the database. */
    size_t size() const { return fMovies.size(); }
    bool empty() const { return fMovies.empty(); }

    /** Access by index. */
    const Movie& at(size_t index) const { return fMovies.at(index); }
    const Movie& operator[](size_t index) const { return fMovies[index]; }

    /** Iteration support. */
    auto begin() const { return fMovies.begin(); }
    auto end() const { return fMovies.end(); }
    auto cbegin() const { return fMovies.cbegin(); }
    auto cend() const { return fMovies.cend(); }

    /** Lookup by id. Returns nullptr if not found. */
    const Movie* findById(int id) const;

    /** All movies. */
    const std::vector<Movie>& movies() const { return fMovies; }

private:
    std::vector<Movie> fMovies;
};
