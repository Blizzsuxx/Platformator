// Diagnostic only: no SDL, SDL_image, SDL_mixer or FluidSynth API calls.
// Build with AddressSanitizer; run outside a ptrace sandbox for leak scanning.
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <link.h>

static int printModule(dl_phdr_info *info, size_t, void *)
{
    std::fprintf(stderr, "MODULE base=%p path=%s\n",
                 reinterpret_cast<void *>(info->dlpi_addr), info->dlpi_name);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    const int cycles = std::atoi(argv[2]);
    if (cycles < 0 || cycles > 10)
        return 2;
    for (int i = 0; i < cycles; ++i)
    {
        void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            std::fprintf(stderr, "%s\n", dlerror());
            return 3;
        }
        if (i == 0)
            dl_iterate_phdr(printModule, nullptr);
        if (dlclose(handle) != 0)
            return 4;
    }
    return 0;
}
