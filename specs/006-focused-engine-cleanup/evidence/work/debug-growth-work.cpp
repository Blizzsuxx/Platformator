#include "debugdraw.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
int main()
{
    std::vector<DebugObject> objects;
    std::size_t relocations = 0, moved = 0, copied = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const auto capacity = objects.capacity();
        std::vector<std::uintptr_t> old;
        if (objects.size() == capacity)
            for (const auto &object : objects) old.push_back(reinterpret_cast<std::uintptr_t>(object.vertices.data()));
        objects.emplace_back(std::vector<Eigen::Vector2f>(16), true, std::string(80, 'x'));
        if (objects.capacity() != capacity)
        {
            ++relocations;
            for (std::size_t j = 0; j < old.size(); ++j)
                if (reinterpret_cast<std::uintptr_t>(objects[j].vertices.data()) == old[j]) ++moved;
                else ++copied;
        }
    }
    std::cout << "objects=" << objects.size() << " growths=" << relocations
              << " preserved_vertex_buffers=" << moved << " replaced_vertex_buffers=" << copied << '\n';
}
