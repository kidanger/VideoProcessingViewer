#include <future>
#include <glob.h>
#include <regex>
#include <tuple>
#ifdef USE_GDAL
#include <gdal.h>
#include <gdal_priv.h>
#endif

#include <alphanum.hpp>
#include <doctest.h>
#include <thread_pool.hpp>

#include "collection_expression.hpp"
#include "fs.hpp"
#include "strutils.hpp"

#define SORT(x) \
    std::sort(x.begin(), x.end(), doj::alphanum_less<std::string>());

static void try_to_read_a_zip(const std::string& path, std::vector<std::string>& filenames)
{
#ifdef USE_GDAL
    std::string zippath = "/vsizip/" + path + "/";
    char** listing = VSIReadDirRecursive(zippath.c_str());
    std::vector<std::string> subfiles;
    if (listing) {
        for (int i = 0; listing[i]; i++) {
            subfiles.push_back(zippath + listing[i]);
        }
        CSLDestroy(listing);
    } else {
        fprintf(stderr, "looks like the zip '%s' is empty\n", path.c_str());
    }

    std::sort(subfiles.begin(), subfiles.end(), doj::alphanum_less<std::string>());
    std::copy(subfiles.cbegin(), subfiles.cend(), std::back_inserter(filenames));
#else
    fprintf(stderr, "reading from zip require GDAL support\n");
#endif
}

static std::vector<std::string> do_glob(const std::string& expr)
{
    glob_t res;
    ::glob(expr.c_str(), GLOB_TILDE | GLOB_NOSORT | GLOB_BRACE, nullptr, &res);
    std::vector<std::string> results;
    for (unsigned int j = 0; j < res.gl_pathc; j++) {
        results.push_back(res.gl_pathv[j]);
    }
    globfree(&res);
    SORT(results);
    return results;
}

static std::vector<std::string> try_split(const std::string& expr)
{
    static std::regex sep { R"(::)" };
    std::vector<std::string> results;
    split(expr, std::back_inserter(results), sep);
    return results;
}

static void list_directory(const std::string& path, std::vector<std::string>& directories, std::vector<std::string>& files)
{
    std::vector<std::string> results;
    using namespace std::filesystem;
    auto it = fs::directory_iterator(path,
        directory_options::follow_directory_symlink | directory_options::skip_permission_denied);
    for (const auto& entry : it) {
        auto& file = entry.path();

        if (file.filename().string()[0] == '.') {
            continue;
        }

        if (entry.is_directory()) {
            directories.push_back(file);
        } else {
            files.push_back(file);
        }
    }
}

std::vector<std::string> collect_directory(const std::string& path)
{
    std::vector<std::string> results;

    std::vector<std::string> directories, files;
    list_directory(path, directories, files);

    std::vector<std::pair<std::string, bool>> sorted;
    for (auto file : files) {
        sorted.push_back(std::make_pair(file, false));
    }
    for (auto dir : directories) {
        sorted.push_back(std::make_pair(dir, true));
    }
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return doj::alphanum_comp(a.first, b.first) < 0; });

    for (auto& info : sorted) {
        const std::string& path = info.first;
        bool is_dir = info.second;
        if (is_dir) {
            auto subfiles = collect_directory(path);
            for (auto f : subfiles) {
                results.push_back(f);
            }
        } else {
            results.push_back(path);
        }
    }
    return results;
}

std::vector<std::string> buildFilenamesFromExpression(const std::string& expr)
{
    std::vector<std::string> filenames;
    printf("start collect\n");

    for (auto subexpr : try_split(expr)) {
        auto globres = do_glob(subexpr);
        for (auto file : globres) {
            if (fs::is_directory(file)) {
                auto indir = collect_directory(file);
                for (auto f : indir)
                    filenames.push_back(f);
            } else {
                filenames.push_back(file);
            }
        }
    }

    printf("end collect\n");

    if (filenames.empty() && expr == "-") {
        filenames.push_back("-");
    }

    printf("%lu\n", filenames.size());
    return filenames;
}

TEST_CASE("buildFilenamesFromExpression")
{
    SUBCASE("-")
    {
        auto v = buildFilenamesFromExpression("-");
        CHECK(v.size() == 1);
        if (v.size() > 0)
            CHECK(v[0] == "-");
    }

    SUBCASE("src (flat)")
    {
        auto v = buildFilenamesFromExpression("src");
        CHECK(v.size() == 70);
        if (v.size() > 0)
            CHECK(v[0] == "src/Colormap.cpp");
        if (v.size() > 1)
            CHECK(v[1] == "src/Colormap.hpp");
    }

    SUBCASE("src/*.cpp (glob)")
    {
        auto v = buildFilenamesFromExpression("src/*.cpp");
        CHECK(v.size() == 33);
        if (v.size() > 0)
            CHECK(v[0] == "src/Colormap.cpp");
        if (v.size() > 1)
            CHECK(v[1] == "src/DisplayArea.cpp");
    }

    SUBCASE("external (recursive)")
    {
        auto v = buildFilenamesFromExpression("external");
        CHECK(v.size() >= 1500);
        if (v.size() > 0)
            CHECK(v[0] == "external/dirent/dirent.h");
        if (v.size() > 1)
            CHECK(v[1] == "external/doctest/doctest.h");
    }

    SUBCASE("src::external (::)")
    {
        auto v1 = buildFilenamesFromExpression("src");
        auto v2 = buildFilenamesFromExpression("external");
        auto v = buildFilenamesFromExpression("src::external");
        CHECK(v.size() == v1.size() + v2.size());
        if (v.size() > 0)
            CHECK(v[0] == v1[0]);
        if (v.size() > 1)
            CHECK(v[1] == v1[1]);
        if (v.size() > v1.size())
            CHECK(v[v1.size()] == v2[0]);
    }
}
