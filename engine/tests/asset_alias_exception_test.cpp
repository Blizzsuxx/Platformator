#include "assetrequestaliases.h"
#include <platformator/texturewrapper.h>
#include <platformator/audiowrapper.h>
#include <platformator/animationclip.h>

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
thread_local long failAfter = -1;
}

void *operator new(std::size_t size)
{
    if (failAfter >= 0 && failAfter-- == 0)
    {
        failAfter = -1;
        throw std::bad_alloc();
    }
    if (void *memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
struct Resource
{
    inline static int alive = 0;
    Resource() { ++alive; }
    ~Resource() { --alive; }
};

void publicationFailures()
{
    const std::string canonical(80, 'c'), request(80, 'r');
    bool failed = false, succeeded = false;
    for (long n = 0; n < 64; ++n)
    {
        std::unordered_map<std::string, std::unique_ptr<Resource>> cache;
        platformator_detail::AssetRequestAliases aliases;
        auto resource = std::make_unique<Resource>();
        failAfter = n;
        try
        {
            auto *result = platformator_detail::publishAsset(cache, aliases, request, canonical,
                                                              std::move(resource));
            failAfter = -1;
            if (!result || cache.size() != 1 || !aliases.find(request))
                throw std::runtime_error("Successful publication missing resource or alias");
            succeeded = true;
        }
        catch (const std::bad_alloc &)
        {
            failAfter = -1;
            failed = true;
            if (!cache.empty() || !aliases.paths.empty() || !aliases.reverse.empty() || Resource::alive != 0)
                throw std::runtime_error("Failed publication retained resource or alias bookkeeping");
        }
        cache.clear();
        if (Resource::alive != 0) throw std::runtime_error("Resource lifetime was not reclaimed");
        if (succeeded) break;
    }
    if (!failed || !succeeded) throw std::runtime_error("Publication allocation sweep incomplete");

    const std::string oldRequest(80, 'o'), newRequest(80, 'n');
    bool existingFailure = false, existingSuccess = false;
    for (long n = 0; n < 64; ++n)
    {
        std::unordered_map<std::string, std::unique_ptr<Resource>> cache;
        platformator_detail::AssetRequestAliases aliases;
        auto *original = platformator_detail::publishAsset(cache, aliases, oldRequest, canonical,
                                                            std::make_unique<Resource>());
        auto extra = std::make_unique<Resource>();
        failAfter = n;
        try
        {
            auto *result = platformator_detail::publishAsset(cache, aliases, newRequest, canonical,
                                                              std::move(extra));
            failAfter = -1;
            if (result != original) throw std::runtime_error("Duplicate publication replaced existing resource");
            existingSuccess = true;
        }
        catch (const std::bad_alloc &)
        {
            failAfter = -1;
            existingFailure = true;
            if (cache.size() != 1 || cache.at(canonical).get() != original || !aliases.find(oldRequest) ||
                aliases.find(newRequest) || aliases.reverse.at(canonical).size() != 1)
                throw std::runtime_error("Failed alias publication disturbed existing resource");
        }
        cache.clear();
        if (Resource::alive != 0) throw std::runtime_error("Existing resource lifetime was not reclaimed");
        if (existingSuccess) break;
    }
    if (!existingFailure || !existingSuccess) throw std::runtime_error("Existing-resource sweep incomplete");
}

template <typename T, typename Factory>
void actualResourcePublication(const char *label, Factory factory)
{
    const std::string request(80, 'r'), canonical(80, 'c');
    bool failed = false, succeeded = false;
    for (long n = 0; n < 32; ++n)
    {
        std::unordered_map<std::string, std::unique_ptr<T>> cache;
        platformator_detail::AssetRequestAliases aliases;
        auto resource = factory();
        failAfter = n;
        try
        {
            auto *result = platformator_detail::publishAsset(cache, aliases, request, canonical,
                                                              std::move(resource));
            failAfter = -1;
            if (!result || cache.size() != 1 || !aliases.find(request))
                throw std::runtime_error(std::string(label) + " publication missing resource or alias");
            succeeded = true;
        }
        catch (const std::bad_alloc &)
        {
            failAfter = -1;
            failed = true;
            if (!cache.empty() || !aliases.paths.empty() || !aliases.reverse.empty())
                throw std::runtime_error(std::string(label) + " failed publication retained state");
        }
        aliases.clear();
        cache.clear();
        if (succeeded) break;
    }
    if (!failed || !succeeded) throw std::runtime_error(std::string(label) + " failure sweep incomplete");
}

void retainedCapacity()
{
    platformator_detail::AssetRequestAliases aliases;
    for (int i = 0; i < 300; ++i)
        aliases.add("request-" + std::to_string(i), "canonical-" + std::to_string(i));
    const auto peakPaths = aliases.paths.bucket_count();
    const auto peakReverse = aliases.reverse.bucket_count();
    if (peakPaths <= 64 || peakReverse <= 64)
        throw std::runtime_error("Capacity fixture did not grow both tables");
    int removed = 0;
    while (aliases.paths.size() > aliases.paths.bucket_count() / 4)
        aliases.remove("canonical-" + std::to_string(removed++));
    const auto next = "canonical-" + std::to_string(removed++);
    failAfter = 0;
    aliases.remove(next);
    if (failAfter != -1) throw std::runtime_error("Reclamation failure was not exercised");
    for (; removed < 299; ++removed) aliases.remove("canonical-" + std::to_string(removed));
    if (!aliases.find("request-299") || aliases.paths.size() != 1 || aliases.reverse.size() != 1 ||
        aliases.paths.bucket_count() > 64 || aliases.reverse.bucket_count() > 64)
        throw std::runtime_error("Alias tables remain oversized after eviction");
    std::cout << "alias buckets peak=" << peakPaths << "/" << peakReverse
              << " after_299_evictions=" << aliases.paths.bucket_count() << "/"
              << aliases.reverse.bucket_count() << '\n';
    aliases.remove("canonical-299");
    aliases.add("again", "again-canonical");
    if (!aliases.find("again")) throw std::runtime_error("Alias table could not be reused");
    aliases.remove("again-canonical");
}
}

int main()
{
    try
    {
        const std::string request(80, 'r');
        const std::string canonical(80, 'c');
        bool observedFailure = false, observedSuccess = false;
        for (long n = 0; n < 32; ++n)
        {
            platformator_detail::AssetRequestAliases aliases;
            failAfter = n;
            try { aliases.add(request, canonical); observedSuccess = true; }
            catch (const std::bad_alloc &)
            {
                failAfter = -1;
                observedFailure = true;
                if (!aliases.paths.empty() || !aliases.reverse.empty())
                    throw std::runtime_error("Failed alias insertion left published bookkeeping");
                aliases.add(request, canonical);
                if (!aliases.find(request)) throw std::runtime_error("Alias retry failed");
            }
            failAfter = -1;
            aliases.remove(canonical);
            if (aliases.find(request)) throw std::runtime_error("Alias survived removal");
            if (observedSuccess) break;
        }
        if (!observedFailure || !observedSuccess) throw std::runtime_error("Failure sweep was incomplete");
        publicationFailures();
        actualResourcePublication<TextureWrapper>("texture", [] { return std::make_unique<TextureWrapper>(nullptr, ""); });
        actualResourcePublication<AudioWrapper>("audio", [] { return std::make_unique<AudioWrapper>(nullptr, ""); });
        actualResourcePublication<AnimationClip>("animation", [] { return std::make_unique<AnimationClip>(); });
        retainedCapacity();
        std::cout << "[PASS] asset alias allocation failure, publication and capacity\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        failAfter = -1;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
