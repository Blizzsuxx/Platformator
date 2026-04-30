
#include <json.hpp>

#define REGISTER_SCRIPT(Type, ...)                                 \
public:                                                            \
    std::string getTypeName() const override { return #Type; }     \
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, __VA_ARGS__) \
    void serialize(nlohmann::json &j) const override               \
    {                                                              \
        to_json(j, *this);                                         \
    }                                                              \
    void deserialize(const nlohmann::json &j) override             \
    {                                                              \
        from_json(j, *this);                                       \
    }                                                              \
                                                                   \
private:                                                           \
    /* This line auto-registers with your factory */               \
    inline static bool _registered = []() { \
            BehaviorFactoryRegistry::registerBehavior(#Type, []() { return new Type(); }); \
            return true; }();
