#include "scene.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "boxcollider.h"
#include "camera.h"
#include "circlecollider.h"
#include "rigidbody.h"
#include "sprite.h"
#include "texturewrapper.h"

namespace
{
    struct RigidbodyConfig
    {
        bool enabled = false;
        BodyType bodyType = DYNAMIC;
        bool gravity = true;
        float mass = 1.0f;
        Eigen::Vector2f velocity = Eigen::Vector2f::Zero();
        Eigen::Vector2f force = Eigen::Vector2f::Zero();
        float angularVelocity = 0.0f;
        float torque = 0.0f;
        float friction = 1.0f;
        float restitution = 0.0f;
    };

    struct BoxColliderConfig
    {
        bool enabled = false;
        float width = 0.0f;
        float height = 0.0f;
        bool isTrigger = false;
        uint64_t collisionGroup = 1;
        uint64_t collisionMask = 1;
    };

    struct CircleColliderConfig
    {
        bool enabled = false;
        float radius = 0.0f;
        bool isTrigger = false;
        uint64_t collisionGroup = 1;
        uint64_t collisionMask = 1;
    };

    struct SpriteConfig
    {
        bool enabled = false;
        std::string path;
        SDL_FlipMode flip = SDL_FLIP_NONE;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct CameraConfig
    {
        bool enabled = false;
        float x = 0.0f;
        float y = 0.0f;
        float width = SCREEN_WIDTH;
        float height = SCREEN_HEIGHT;
    };

    struct ObjectConfig
    {
        std::string name;
        std::string tag;
        bool active = true;
        Eigen::Vector2f position = Eigen::Vector2f::Zero();
        Eigen::Vector2f scale = Eigen::Vector2f::Ones();
        float rotation = 0.0f;
        RigidbodyConfig rigidbody;
        BoxColliderConfig boxCollider;
        CircleColliderConfig circleCollider;
        SpriteConfig sprite;
        CameraConfig camera;
    };

    class TokenStream
    {
    public:
        explicit TokenStream(std::vector<std::string> tokens) : tokens(std::move(tokens)), index(0)
        {
        }

        bool empty() const
        {
            return index >= tokens.size();
        }

        std::string consume()
        {
            if (empty())
            {
                throw std::runtime_error("Unexpected end of scene file.");
            }

            return tokens[index++];
        }

        bool match(const std::string &expected)
        {
            if (!empty() && tokens[index] == expected)
            {
                index++;
                return true;
            }

            return false;
        }

        void expect(const std::string &expected)
        {
            std::string token = consume();
            if (token != expected)
            {
                throw std::runtime_error("Expected '" + expected + "' but found '" + token + "'.");
            }
        }

    private:
        std::vector<std::string> tokens;
        size_t index;
    };

    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    std::vector<std::string> tokenize(const std::string &contents)
    {
        std::vector<std::string> tokens;
        size_t index = 0;

        while (index < contents.size())
        {
            char currentCharacter = contents[index];

            if (std::isspace(static_cast<unsigned char>(currentCharacter)))
            {
                index++;
                continue;
            }

            if (currentCharacter == '#')
            {
                while (index < contents.size() && contents[index] != '\n')
                {
                    index++;
                }
                continue;
            }

            if (currentCharacter == '/' && index + 1 < contents.size() && contents[index + 1] == '/')
            {
                index += 2;
                while (index < contents.size() && contents[index] != '\n')
                {
                    index++;
                }
                continue;
            }

            if (currentCharacter == '{' || currentCharacter == '}')
            {
                tokens.emplace_back(1, currentCharacter);
                index++;
                continue;
            }

            if (currentCharacter == '"')
            {
                index++;
                std::string token;

                while (index < contents.size())
                {
                    char stringCharacter = contents[index++];
                    if (stringCharacter == '"')
                    {
                        break;
                    }

                    if (stringCharacter == '\\' && index < contents.size())
                    {
                        token.push_back(contents[index++]);
                        continue;
                    }

                    token.push_back(stringCharacter);
                }

                tokens.push_back(token);
                continue;
            }

            size_t start = index;
            while (index < contents.size())
            {
                char tokenCharacter = contents[index];
                if (std::isspace(static_cast<unsigned char>(tokenCharacter)) || tokenCharacter == '{' || tokenCharacter == '}' || tokenCharacter == '#')
                {
                    break;
                }

                if (tokenCharacter == '/' && index + 1 < contents.size() && contents[index + 1] == '/')
                {
                    break;
                }

                index++;
            }

            tokens.push_back(contents.substr(start, index - start));
        }

        return tokens;
    }

    bool parseBoolToken(const std::string &token)
    {
        std::string lowerCaseToken = toLower(token);
        if (lowerCaseToken == "true" || lowerCaseToken == "1" || lowerCaseToken == "yes")
        {
            return true;
        }

        if (lowerCaseToken == "false" || lowerCaseToken == "0" || lowerCaseToken == "no")
        {
            return false;
        }

        throw std::runtime_error("Invalid boolean value '" + token + "'.");
    }

    float parseFloatToken(const std::string &token)
    {
        try
        {
            return std::stof(token);
        }
        catch (const std::exception &)
        {
            throw std::runtime_error("Invalid float value '" + token + "'.");
        }
    }

    uint64_t parseUInt64Token(const std::string &token)
    {
        try
        {
            return static_cast<uint64_t>(std::stoull(token));
        }
        catch (const std::exception &)
        {
            throw std::runtime_error("Invalid unsigned integer value '" + token + "'.");
        }
    }

    Eigen::Vector2f parseVector2(TokenStream &tokens)
    {
        float x = parseFloatToken(tokens.consume());
        float y = parseFloatToken(tokens.consume());
        return Eigen::Vector2f(x, y);
    }

    SDL_FlipMode parseFlipMode(const std::string &token)
    {
        std::string lowerCaseToken = toLower(token);
        if (lowerCaseToken == "none")
        {
            return SDL_FLIP_NONE;
        }
        if (lowerCaseToken == "horizontal")
        {
            return SDL_FLIP_HORIZONTAL;
        }
        if (lowerCaseToken == "vertical")
        {
            return SDL_FLIP_VERTICAL;
        }
        if (lowerCaseToken == "both")
        {
            return static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL);
        }

        throw std::runtime_error("Invalid sprite flip mode '" + token + "'.");
    }

    BodyType parseBodyType(const std::string &token)
    {
        std::string lowerCaseToken = toLower(token);
        if (lowerCaseToken == "dynamic")
        {
            return DYNAMIC;
        }
        if (lowerCaseToken == "static")
        {
            return STATIC;
        }
        if (lowerCaseToken == "kinematic")
        {
            return KINEMATIC;
        }

        throw std::runtime_error("Invalid rigidbody bodyType '" + token + "'.");
    }

    std::string resolveSceneRelativePath(const std::string &sceneFilePath, const std::string &resourcePath)
    {
        std::filesystem::path resolvedPath(resourcePath);
        if (resolvedPath.is_relative())
        {
            resolvedPath = std::filesystem::path(sceneFilePath).parent_path() / resolvedPath;
        }

        return resolvedPath.lexically_normal().string();
    }

    std::string escapeSceneString(const std::string &value)
    {
        std::string escaped;
        escaped.reserve(value.size());

        for (char character : value)
        {
            if (character == '\\' || character == '"')
            {
                escaped.push_back('\\');
            }
            escaped.push_back(character);
        }

        return escaped;
    }

    std::string formatFloat(float value)
    {
        if (!std::isfinite(value))
        {
            throw std::runtime_error("Cannot save scene values that are not finite.");
        }

        std::ostringstream stream;
        stream << std::setprecision(9) << value;
        return stream.str();
    }

    std::string boolToToken(bool value)
    {
        return value ? "true" : "false";
    }

    std::string bodyTypeToToken(BodyType bodyType)
    {
        switch (bodyType)
        {
        case DYNAMIC:
            return "dynamic";
        case STATIC:
            return "static";
        case KINEMATIC:
            return "kinematic";
        default:
            throw std::runtime_error("Cannot save scene with an unknown rigidbody type.");
        }
    }

    std::string flipModeToToken(SDL_FlipMode flipMode)
    {
        if (flipMode == SDL_FLIP_NONE)
        {
            return "none";
        }
        if (flipMode == SDL_FLIP_HORIZONTAL)
        {
            return "horizontal";
        }
        if (flipMode == SDL_FLIP_VERTICAL)
        {
            return "vertical";
        }
        if (flipMode == static_cast<SDL_FlipMode>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL))
        {
            return "both";
        }

        throw std::runtime_error("Cannot save scene with an unsupported sprite flip mode.");
    }

    std::string makeSceneResourcePath(const std::string &sceneFilePath, const std::string &resourcePath)
    {
        if (resourcePath.empty())
        {
            return resourcePath;
        }

        std::filesystem::path sceneDirectory = std::filesystem::absolute(std::filesystem::path(sceneFilePath)).parent_path();
        std::filesystem::path normalizedResourcePath = std::filesystem::path(resourcePath);
        if (normalizedResourcePath.is_relative())
        {
            normalizedResourcePath = std::filesystem::absolute(normalizedResourcePath);
        }
        normalizedResourcePath = normalizedResourcePath.lexically_normal();

        if (sceneDirectory.empty())
        {
            return normalizedResourcePath.generic_string();
        }

        std::error_code errorCode;
        std::filesystem::path relativePath = std::filesystem::relative(normalizedResourcePath, sceneDirectory, errorCode);
        if (!errorCode && !relativePath.empty())
        {
            return relativePath.generic_string();
        }

        return normalizedResourcePath.generic_string();
    }

    std::string indent(size_t level)
    {
        return std::string(level * 4, ' ');
    }

    void writeKeyValueLine(std::ostream &stream, size_t indentLevel, const std::string &key, const std::string &value)
    {
        stream << indent(indentLevel) << key << ' ' << value << '\n';
    }

    void writeStringLine(std::ostream &stream, size_t indentLevel, const std::string &key, const std::string &value)
    {
        writeKeyValueLine(stream, indentLevel, key, '"' + escapeSceneString(value) + '"');
    }

    void parseRigidbodyBlock(TokenStream &tokens, RigidbodyConfig &config)
    {
        config.enabled = true;
        tokens.expect("{");

        while (true)
        {
            if (tokens.match("}"))
            {
                return;
            }

            std::string key = toLower(tokens.consume());
            if (key == "bodytype")
            {
                config.bodyType = parseBodyType(tokens.consume());
            }
            else if (key == "gravity")
            {
                config.gravity = parseBoolToken(tokens.consume());
            }
            else if (key == "mass")
            {
                config.mass = parseFloatToken(tokens.consume());
            }
            else if (key == "velocity")
            {
                config.velocity = parseVector2(tokens);
            }
            else if (key == "force")
            {
                config.force = parseVector2(tokens);
            }
            else if (key == "angularvelocity")
            {
                config.angularVelocity = parseFloatToken(tokens.consume());
            }
            else if (key == "torque")
            {
                config.torque = parseFloatToken(tokens.consume());
            }
            else if (key == "friction")
            {
                config.friction = parseFloatToken(tokens.consume());
            }
            else if (key == "restitution")
            {
                config.restitution = parseFloatToken(tokens.consume());
            }
            else
            {
                throw std::runtime_error("Unknown rigidbody property '" + key + "'.");
            }
        }
    }

    void parseBoxColliderBlock(TokenStream &tokens, BoxColliderConfig &config)
    {
        config.enabled = true;
        tokens.expect("{");

        while (true)
        {
            if (tokens.match("}"))
            {
                return;
            }

            std::string key = toLower(tokens.consume());
            if (key == "size")
            {
                config.width = parseFloatToken(tokens.consume());
                config.height = parseFloatToken(tokens.consume());
            }
            else if (key == "width")
            {
                config.width = parseFloatToken(tokens.consume());
            }
            else if (key == "height")
            {
                config.height = parseFloatToken(tokens.consume());
            }
            else if (key == "trigger" || key == "istrigger")
            {
                config.isTrigger = parseBoolToken(tokens.consume());
            }
            else if (key == "collisiongroup" || key == "group")
            {
                config.collisionGroup = parseUInt64Token(tokens.consume());
            }
            else if (key == "collisionmask" || key == "mask")
            {
                config.collisionMask = parseUInt64Token(tokens.consume());
            }
            else
            {
                throw std::runtime_error("Unknown boxCollider property '" + key + "'.");
            }
        }
    }

    void parseCircleColliderBlock(TokenStream &tokens, CircleColliderConfig &config)
    {
        config.enabled = true;
        tokens.expect("{");

        while (true)
        {
            if (tokens.match("}"))
            {
                return;
            }

            std::string key = toLower(tokens.consume());
            if (key == "radius")
            {
                config.radius = parseFloatToken(tokens.consume());
            }
            else if (key == "trigger" || key == "istrigger")
            {
                config.isTrigger = parseBoolToken(tokens.consume());
            }
            else if (key == "collisiongroup" || key == "group")
            {
                config.collisionGroup = parseUInt64Token(tokens.consume());
            }
            else if (key == "collisionmask" || key == "mask")
            {
                config.collisionMask = parseUInt64Token(tokens.consume());
            }
            else
            {
                throw std::runtime_error("Unknown circleCollider property '" + key + "'.");
            }
        }
    }

    void parseSpriteBlock(TokenStream &tokens, SpriteConfig &config)
    {
        config.enabled = true;
        tokens.expect("{");

        while (true)
        {
            if (tokens.match("}"))
            {
                return;
            }

            std::string key = toLower(tokens.consume());
            if (key == "path")
            {
                config.path = tokens.consume();
            }
            else if (key == "flip")
            {
                config.flip = parseFlipMode(tokens.consume());
            }
            else if (key == "size")
            {
                config.width = parseFloatToken(tokens.consume());
                config.height = parseFloatToken(tokens.consume());
            }
            else if (key == "width")
            {
                config.width = parseFloatToken(tokens.consume());
            }
            else if (key == "height")
            {
                config.height = parseFloatToken(tokens.consume());
            }
            else
            {
                throw std::runtime_error("Unknown sprite property '" + key + "'.");
            }
        }
    }

    void parseCameraBlock(TokenStream &tokens, CameraConfig &config)
    {
        config.enabled = true;
        tokens.expect("{");

        while (true)
        {
            if (tokens.match("}"))
            {
                return;
            }

            std::string key = toLower(tokens.consume());
            if (key == "viewport")
            {
                config.x = parseFloatToken(tokens.consume());
                config.y = parseFloatToken(tokens.consume());
                config.width = parseFloatToken(tokens.consume());
                config.height = parseFloatToken(tokens.consume());
            }
            else if (key == "position")
            {
                config.x = parseFloatToken(tokens.consume());
                config.y = parseFloatToken(tokens.consume());
            }
            else if (key == "size")
            {
                config.width = parseFloatToken(tokens.consume());
                config.height = parseFloatToken(tokens.consume());
            }
            else
            {
                throw std::runtime_error("Unknown camera property '" + key + "'.");
            }
        }
    }

    ObjectConfig parseObject(TokenStream &tokens)
    {
        std::string objectKeyword = toLower(tokens.consume());
        if (objectKeyword != "object")
        {
            throw std::runtime_error("Expected 'object' but found '" + objectKeyword + "'.");
        }

        tokens.expect("{");

        ObjectConfig config;
        while (true)
        {
            if (tokens.match("}"))
            {
                return config;
            }

            std::string key = toLower(tokens.consume());
            if (key == "name")
            {
                config.name = tokens.consume();
            }
            else if (key == "tag")
            {
                config.tag = tokens.consume();
            }
            else if (key == "active")
            {
                config.active = parseBoolToken(tokens.consume());
            }
            else if (key == "position")
            {
                config.position = parseVector2(tokens);
            }
            else if (key == "scale")
            {
                config.scale = parseVector2(tokens);
            }
            else if (key == "rotation")
            {
                config.rotation = parseFloatToken(tokens.consume());
            }
            else if (key == "rigidbody")
            {
                parseRigidbodyBlock(tokens, config.rigidbody);
            }
            else if (key == "boxcollider")
            {
                parseBoxColliderBlock(tokens, config.boxCollider);
            }
            else if (key == "circlecollider")
            {
                parseCircleColliderBlock(tokens, config.circleCollider);
            }
            else if (key == "sprite")
            {
                parseSpriteBlock(tokens, config.sprite);
            }
            else if (key == "camera")
            {
                parseCameraBlock(tokens, config.camera);
            }
            else
            {
                throw std::runtime_error("Unknown object property or component '" + key + "'.");
            }
        }
    }
} // namespace

Scene::Scene(std::string filepath) : filePath(std::move(filepath))
{
}

Scene::~Scene()
{
}

std::vector<GameObject *> Scene::loadScene()
{
    std::ifstream sceneFile(filePath);
    if (!sceneFile.is_open())
    {
        throw std::runtime_error("Failed to open scene file '" + filePath + "'.");
    }

    std::stringstream buffer;
    buffer << sceneFile.rdbuf();

    TokenStream tokens(tokenize(buffer.str()));
    std::vector<GameObject *> objects;

    try
    {
        while (!tokens.empty())
        {
            ObjectConfig config = parseObject(tokens);

            if (config.boxCollider.enabled && config.circleCollider.enabled)
            {
                throw std::runtime_error("A scene object cannot define both boxCollider and circleCollider.");
            }

            GameObject *gameObject = new GameObject(config.rotation, config.active, config.position, config.scale, config.name, config.tag);

            try
            {
                if (config.camera.enabled)
                {
                    gameObject->addComponentInternal(new Camera(gameObject, config.camera.x, config.camera.y, config.camera.width, config.camera.height));
                }

                if (config.rigidbody.enabled)
                {
                    Rigidbody *rigidbody = new Rigidbody(gameObject, config.rigidbody.bodyType, config.rigidbody.gravity);
                    rigidbody->setMass(config.rigidbody.mass);
                    rigidbody->setVelocity(config.rigidbody.velocity);
                    rigidbody->setForce(config.rigidbody.force);
                    rigidbody->setAngularVelocity(config.rigidbody.angularVelocity);
                    rigidbody->setTorque(config.rigidbody.torque);
                    rigidbody->setFriction(config.rigidbody.friction);
                    rigidbody->setRestitution(config.rigidbody.restitution);
                    gameObject->addComponentInternal(rigidbody);
                }

                if (config.boxCollider.enabled)
                {
                    if (config.boxCollider.width <= 0.0f || config.boxCollider.height <= 0.0f)
                    {
                        throw std::runtime_error("boxCollider requires positive width and height.");
                    }

                    BoxCollider *boxCollider = new BoxCollider(gameObject, config.boxCollider.width, config.boxCollider.height);
                    boxCollider->setIsTrigger(config.boxCollider.isTrigger);
                    boxCollider->setCollisionGroup(config.boxCollider.collisionGroup);
                    boxCollider->setCollisionMask(config.boxCollider.collisionMask);
                    gameObject->addComponentInternal(boxCollider);
                }

                if (config.circleCollider.enabled)
                {
                    if (config.circleCollider.radius <= 0.0f)
                    {
                        throw std::runtime_error("circleCollider requires a positive radius.");
                    }

                    CircleCollider *circleCollider = new CircleCollider(gameObject, config.circleCollider.radius);
                    circleCollider->setIsTrigger(config.circleCollider.isTrigger);
                    circleCollider->setCollisionGroup(config.circleCollider.collisionGroup);
                    circleCollider->setCollisionMask(config.circleCollider.collisionMask);
                    gameObject->addComponentInternal(circleCollider);
                }

                if (config.sprite.enabled)
                {
                    Sprite *sprite = nullptr;
                    if (config.sprite.path.empty())
                    {
                        sprite = new Sprite(gameObject);
                        sprite->setFlip(config.sprite.flip);
                        sprite->setWidth(config.sprite.width);
                        sprite->setHeight(config.sprite.height);
                    }
                    else
                    {
                        std::string resolvedPath = resolveSceneRelativePath(filePath, config.sprite.path);
                        sprite = new Sprite(gameObject, resolvedPath.c_str(), config.sprite.flip, config.sprite.width, config.sprite.height);
                    }

                    gameObject->addComponentInternal(sprite);
                }

                objects.push_back(gameObject);
            }
            catch (const std::exception &)
            {
                delete gameObject;
                throw;
            }
        }
    }
    catch (const std::exception &exception)
    {
        for (GameObject *gameObject : objects)
        {
            delete gameObject;
        }

        throw std::runtime_error("Failed to load scene '" + filePath + "': " + exception.what());
    }

    return objects;
}

void Scene::saveScene(const std::list<GameObject *> &gameObjects) const
{
    std::filesystem::path scenePath(filePath);
    std::filesystem::path sceneDirectory = scenePath.parent_path();
    if (!sceneDirectory.empty())
    {
        std::filesystem::create_directories(sceneDirectory);
    }

    std::ofstream sceneFile(filePath);
    if (!sceneFile.is_open())
    {
        throw std::runtime_error("Failed to open scene file '" + filePath + "' for writing.");
    }

    bool wroteAnyObjects = false;
    for (GameObject *gameObject : gameObjects)
    {
        if (gameObject == nullptr || gameObject->getIsMarkedForDeletion())
        {
            continue;
        }

        Rigidbody *rigidbody = gameObject->getComponent<Rigidbody>();
        Collider *collider = static_cast<Collider *>(gameObject->getComponent(ComponentType::COLLIDER));
        Sprite *sprite = gameObject->getComponent<Sprite>();
        Camera *camera = gameObject->getComponent<Camera>();

        if (wroteAnyObjects)
        {
            sceneFile << '\n';
        }
        wroteAnyObjects = true;

        sceneFile << "object {\n";
        writeStringLine(sceneFile, 1, "name", gameObject->getName());
        if (!gameObject->getTag().empty())
        {
            writeStringLine(sceneFile, 1, "tag", gameObject->getTag());
        }
        writeKeyValueLine(sceneFile, 1, "active", boolToToken(gameObject->getActive()));
        sceneFile << indent(1) << "position " << formatFloat(gameObject->getPosition().x()) << ' ' << formatFloat(gameObject->getPosition().y()) << '\n';
        sceneFile << indent(1) << "rotation " << formatFloat(gameObject->getRotation()) << '\n';
        sceneFile << indent(1) << "scale " << formatFloat(gameObject->getScale().x()) << ' ' << formatFloat(gameObject->getScale().y()) << '\n';

        if (camera != nullptr)
        {
            const SDL_FRect &cameraRect = camera->getCamera();
            sceneFile << indent(1) << "camera {\n";
            sceneFile << indent(2) << "viewport " << formatFloat(cameraRect.x) << ' ' << formatFloat(cameraRect.y) << ' ' << formatFloat(cameraRect.w) << ' ' << formatFloat(cameraRect.h) << '\n';
            sceneFile << indent(1) << "}\n";
        }

        if (rigidbody != nullptr)
        {
            sceneFile << indent(1) << "rigidbody {\n";
            writeKeyValueLine(sceneFile, 2, "bodyType", bodyTypeToToken(rigidbody->getBodyType()));
            writeKeyValueLine(sceneFile, 2, "gravity", boolToToken(rigidbody->getGravity()));
            writeKeyValueLine(sceneFile, 2, "mass", formatFloat(rigidbody->getMass()));
            sceneFile << indent(2) << "velocity " << formatFloat(rigidbody->getVelocity().x()) << ' ' << formatFloat(rigidbody->getVelocity().y()) << '\n';
            sceneFile << indent(2) << "force " << formatFloat(rigidbody->getForce().x()) << ' ' << formatFloat(rigidbody->getForce().y()) << '\n';
            writeKeyValueLine(sceneFile, 2, "angularVelocity", formatFloat(rigidbody->getAngularVelocity()));
            writeKeyValueLine(sceneFile, 2, "torque", formatFloat(rigidbody->getTorque()));
            writeKeyValueLine(sceneFile, 2, "friction", formatFloat(rigidbody->getFriction()));
            writeKeyValueLine(sceneFile, 2, "restitution", formatFloat(rigidbody->getRestitution()));
            sceneFile << indent(1) << "}\n";
        }

        if (collider != nullptr)
        {
            if (collider->getColliderType() == ColliderType::BoxCollider)
            {
                BoxCollider *boxCollider = static_cast<BoxCollider *>(collider);
                float scaleX = gameObject->getScale().x();
                float scaleY = gameObject->getScale().y();
                float unscaledWidth = std::abs(scaleX) > 1e-6f ? boxCollider->getWidth() / scaleX : boxCollider->getWidth();
                float unscaledHeight = std::abs(scaleY) > 1e-6f ? boxCollider->getHeight() / scaleY : boxCollider->getHeight();

                sceneFile << indent(1) << "boxCollider {\n";
                sceneFile << indent(2) << "size " << formatFloat(unscaledWidth) << ' ' << formatFloat(unscaledHeight) << '\n';
                writeKeyValueLine(sceneFile, 2, "trigger", boolToToken(boxCollider->getIsTrigger()));
                writeKeyValueLine(sceneFile, 2, "collisionGroup", std::to_string(boxCollider->getCollisionGroup()));
                writeKeyValueLine(sceneFile, 2, "collisionMask", std::to_string(boxCollider->getCollisionMask()));
                sceneFile << indent(1) << "}\n";
            }
            else if (collider->getColliderType() == ColliderType::CircleCollider)
            {
                CircleCollider *circleCollider = static_cast<CircleCollider *>(collider);
                float scaleX = std::abs(gameObject->getScale().x());
                float scaleY = std::abs(gameObject->getScale().y());
                float maxScale = std::max(scaleX, scaleY);
                float unscaledRadius = maxScale > 1e-6f ? circleCollider->getRadius() / maxScale : circleCollider->getRadius();

                sceneFile << indent(1) << "circleCollider {\n";
                writeKeyValueLine(sceneFile, 2, "radius", formatFloat(unscaledRadius));
                writeKeyValueLine(sceneFile, 2, "trigger", boolToToken(circleCollider->getIsTrigger()));
                writeKeyValueLine(sceneFile, 2, "collisionGroup", std::to_string(circleCollider->getCollisionGroup()));
                writeKeyValueLine(sceneFile, 2, "collisionMask", std::to_string(circleCollider->getCollisionMask()));
                sceneFile << indent(1) << "}\n";
            }
        }

        if (sprite != nullptr)
        {
            sceneFile << indent(1) << "sprite {\n";
            TextureWrapper *textureWrapper = sprite->getTextureWrapper();
            if (textureWrapper != nullptr)
            {
                writeStringLine(sceneFile, 2, "path", makeSceneResourcePath(filePath, textureWrapper->getFilePath()));
            }
            writeKeyValueLine(sceneFile, 2, "flip", flipModeToToken(sprite->getFlip()));
            sceneFile << indent(2) << "size " << formatFloat(sprite->getWidth()) << ' ' << formatFloat(sprite->getHeight()) << '\n';
            sceneFile << indent(1) << "}\n";
        }

        sceneFile << "}\n";
    }
}