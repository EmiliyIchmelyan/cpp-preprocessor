#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using filesystem::path;

path operator""_p(const char* data, std::size_t sz) {
    return path(data, data + sz);
}

// Регулярные выражения для #include "..." и #include <...>
static const regex INCLUDE_QUOTE_REG(R"/(\s*#\s*include\s*"([^"]*)"\s*)/");
static const regex INCLUDE_ANGLE_REG(R"/(\s*#\s*include\s*<([^>]*)>\s*)/");

// Рекурсивно обрабатывает файл: читает строки и подставляет include-ы.
bool ProcessStream(istream& in, ostream& out, const path& in_file_path,
                   const vector<path>& include_directories) {
    string line{};
    int line_number = 0;
    smatch matches;

    while (getline(in, line)) {
        ++line_number;

        const bool is_quote_include = regex_match(line, matches, INCLUDE_QUOTE_REG);
        const bool is_angle_include =
            !is_quote_include && regex_match(line, matches, INCLUDE_ANGLE_REG);

        if (!is_quote_include && !is_angle_include) {
            out << line << '\n';
            continue;
        }

        const string included_name = matches[1].str();
        const path included_path(included_name);

        // Формируем список мест для поиска файла в порядке приоритета:
        // 1. Для кавычек — директория текущего файла.
        // 2. Всегда — переданные include_directories.
        vector<path> candidates;
        if (is_quote_include) {
            candidates.push_back(in_file_path.parent_path() / included_path);
        }
        for (const path& dir : include_directories) {
            candidates.push_back(dir / included_path);
        }

        bool found = false;
        for (const path& candidate : candidates) {
            ifstream inc_in(candidate);
            if (inc_in.is_open()) {
                if (!ProcessStream(inc_in, out, candidate, include_directories)) {
                    return false;
                }
                found = true;
                break;
            }
        }

        if (!found) {
            cout << "unknown include file " << included_name
                 << " at file " << in_file_path.string()
                 << " at line " << line_number << endl;
            return false;
        }
    }

    return true;
}

bool Preprocess(const path& in_file, const path& out_file,
                const vector<path>& include_directories) {
    // Если входной файл не открывается, ничего не пишем и не создаем выходной.
    ifstream in(in_file);
    if (!in.is_open()) {
        return false;
    }

    ofstream out(out_file);
    if (!out.is_open()) {
        return false;
    }
    return ProcessStream(in, out, in_file, include_directories);
}

string GetFileContents(string file) {
    ifstream stream(file);
    return {(istreambuf_iterator<char>(stream)), istreambuf_iterator<char>()};
}

void Test() {
    error_code err;
    filesystem::remove_all("sources"_p, err);
    filesystem::create_directories("sources"_p / "include2"_p / "lib"_p, err);
    filesystem::create_directories("sources"_p / "include1"_p, err);
    filesystem::create_directories("sources"_p / "dir1"_p / "subdir"_p, err);

    {
        ofstream file("sources/a.cpp");
        file << "// this comment before include\n"
                "#include \"dir1/b.h\"\n"
                "// text between b.h and c.h\n"
                "#include \"dir1/d.h\"\n"
                "\n"
                "int SayHello() {\n"
                "    cout << \"hello, world!\" << endl;\n"
                "#   include<dummy.txt>\n"
                "}\n"s;
    }
    {
        ofstream file("sources/dir1/b.h");
        file << "// text from b.h before include\n"
                "#include \"subdir/c.h\"\n"
                "// text from b.h after include"s;
    }
    {
        ofstream file("sources/dir1/subdir/c.h");
        file << "// text from c.h before include\n"
                "#include <std1.h>\n"
                "// text from c.h after include\n"s;
    }
    {
        ofstream file("sources/dir1/d.h");
        file << "// text from d.h before include\n"
                "#include \"lib/std2.h\"\n"
                "// text from d.h after include\n"s;
    }
    {
        ofstream file("sources/include1/std1.h");
        file << "// std1\n"s;
    }
    {
        ofstream file("sources/include2/lib/std2.h");
        file << "// std2\n"s;
    }

    assert((!Preprocess("sources"_p / "a.cpp"_p, "sources"_p / "a.in"_p,
                                  {"sources"_p / "include1"_p,"sources"_p / "include2"_p})));

    ostringstream test_out;
    test_out << "// this comment before include\n"
                "// text from b.h before include\n"
                "// text from c.h before include\n"
                "// std1\n"
                "// text from c.h after include\n"
                "// text from b.h after include\n"
                "// text between b.h and c.h\n"
                "// text from d.h before include\n"
                "// std2\n"
                "// text from d.h after include\n"
                "\n"
                "int SayHello() {\n"
                "    cout << \"hello, world!\" << endl;\n"s;

    assert(GetFileContents("sources/a.in"s) == test_out.str());
}

int main() {
    Test();
}