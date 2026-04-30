
class Behavior;

class BehaviorFactoryRegistry
{
public:
    static BehaviorFactoryRegistry &getInstance()
    {
        static BehaviorFactoryRegistry instance;
        return instance;
    }

    BehaviorFactoryRegistry(const BehaviorFactoryRegistry &) = delete;
    BehaviorFactoryRegistry &operator=(const BehaviorFactoryRegistry &) = delete;

    void registerFactory(const std::string &typeName, std::function<Behavior *()> factory);
    Behavior *createBehavior(const std::string &typeName) const;

private:
    BehaviorFactoryRegistry() = default;
    ~BehaviorFactoryRegistry() = default;
    std::unordered_map<std::string, std::function<Behavior *()>> factories;
};
