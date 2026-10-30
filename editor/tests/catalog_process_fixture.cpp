#include <iostream>
#include <string_view>

int main(int argc, char **argv)
{
    const std::string_view mode = argc > 1 ? argv[1] : "valid";
    if (mode == "fail")
    {
        std::cerr << "fixture failure\n";
        return 7;
    }
    if (mode != "valid")
        return 8;
    std::cerr << "fixture diagnostic\n";
    std::cout << R"({"schema_version":1,"behaviors":[]})";
    return 0;
}
