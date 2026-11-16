#pragma once
#include <json.hpp>
#include "platformator/referenceresolver.h"
class Sprite;
class Audio;
class Animator;
struct AnimationFrame;
struct AnimationClip;
void deserialize(const nlohmann::json &, Sprite &, platformator::ReferenceResolver &);
void deserialize(const nlohmann::json &, Audio &, platformator::ReferenceResolver &);
void deserialize(const nlohmann::json &, Animator &, platformator::ReferenceResolver &);
void deserialize(const nlohmann::json &, AnimationFrame &, platformator::ReferenceResolver &);
void deserialize(const nlohmann::json &, AnimationClip &, platformator::ReferenceResolver &);
