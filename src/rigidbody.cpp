#include "platformator/rigidbody.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "constants.h"
#include "gamemanager.h"
#include "runtimeaccess.h"
#include "inputvalidation.h"

Rigidbody::Rigidbody() : Rigidbody(nullptr, BodyType::DYNAMIC, true)
{
}

Rigidbody::Rigidbody(GameObject *gameObject) : Rigidbody(gameObject, BodyType::DYNAMIC, true)
{
}

Rigidbody::Rigidbody(GameObject *gameObject, BodyType bodyType, bool gravity)
    : Component(gameObject, ComponentType::RIGID_BODY), velocity(0.0f, 0.0f), force(0.0f, 0.0f), mass(1.0f),
      inverseMass(1.0f / mass), angularVelocity(0.0f), torque(0.0f), momentOfInertia(1.0f),
      inverseMomentOfInertia(1.0f / momentOfInertia), friction(1.0f), restitution(0.0f), bodyType(bodyType),
      gravity(gravity), sleeping(false), supportContactCount(0), sleepTimer(0.0), isRegisteredInPhysicsManager(false),
      physicsManagerIndex(SIZE_MAX)
{
    initialize();
}

Rigidbody::~Rigidbody()
{
}

void Rigidbody::initialize()
{
    if (bodyType < DYNAMIC || bodyType > KINEMATIC)
        throw std::invalid_argument("Unknown body type");
    refreshMomentOfInertiaCache();

    if (bodyType != BodyType::DYNAMIC)
    {
        force = Eigen::Vector2f::Zero();
        torque = 0.0f;
        sleepTimer = 0.0;

        if (bodyType == BodyType::STATIC)
        {
            velocity = Eigen::Vector2f::Zero();
            angularVelocity = 0.0f;
        }
    }
}

float Rigidbody::calculateAutomaticMomentOfInertia() const
{
    if (mass <= 0.0f)
    {
        return 0.0f;
    }

    GameObject *gameObject = getGameObject();
    if (gameObject == nullptr)
    {
        return 1.0f;
    }

    Collider *collider = (Collider *)gameObject->getComponent(ComponentType::COLLIDER);
    if (collider == nullptr)
    {
        return 1.0f;
    }

    switch (collider->getColliderType())
    {
    case ColliderType::BoxCollider: {
        const BoxCollider *boxCollider = (const BoxCollider *)collider;
        float width = boxCollider->getWidth();
        float height = boxCollider->getHeight();
        return mass * (width * width + height * height) / 12.0f;
    }
    case ColliderType::CircleCollider: {
        const CircleCollider *circleCollider = (const CircleCollider *)collider;
        float radius = circleCollider->getRadius();
        return 0.5f * mass * radius * radius;
    }
    default:
        return momentOfInertia;
    }
}

Rigidbody *Rigidbody::refreshMomentOfInertiaCache()
{
    momentOfInertia = calculateAutomaticMomentOfInertia();
    inverseMomentOfInertia = momentOfInertia > 0.0f ? 1.0f / momentOfInertia : 0.0f;

    return this;
}

// Getters
const Eigen::Vector2f &Rigidbody::getVelocity() const
{
    return velocity;
}

const Eigen::Vector2f &Rigidbody::getForce() const
{
    return force;
}

float Rigidbody::getMass() const
{
    return mass;
}

float Rigidbody::getAngularVelocity() const
{
    return angularVelocity;
}

float Rigidbody::getTorque() const
{
    return torque;
}

float Rigidbody::getMomentOfInertia() const
{
    return momentOfInertia;
}

float Rigidbody::getFriction() const
{
    return friction;
}

float Rigidbody::getRestitution() const
{
    return restitution;
}

BodyType Rigidbody::getBodyType() const
{
    return bodyType;
}

bool Rigidbody::getGravity() const
{
    return gravity;
}

// Setters
Rigidbody *Rigidbody::setVelocity(const Eigen::Vector2f &velocity)
{
    if (!velocity.allFinite())
        throw std::invalid_argument("body.velocity must be finite");
    if (getIsSleeping() && velocity.squaredNorm() > WAKE_LINEAR_EPSILON_SQUARED)
    {
        wakeUp();
    }

    if (bodyType == BodyType::STATIC)
    {
        return this;
    }

    this->velocity = velocity;
    if (bodyType == KINEMATIC)
        queueParticipationChange();
    return this;
}

Rigidbody *Rigidbody::setForce(const Eigen::Vector2f &force)
{
    if (!force.allFinite())
        throw std::invalid_argument("body.force must be finite");
    if (getIsSleeping() && force.squaredNorm() > WAKE_LINEAR_EPSILON_SQUARED)
    {
        wakeUp();
    }

    this->force = force;
    return this;
}

Rigidbody *Rigidbody::addForce(const Eigen::Vector2f &force)
{
    if (!force.allFinite())
        throw std::invalid_argument("body.force must be finite");
    if (getIsSleeping() && force.squaredNorm() > WAKE_LINEAR_EPSILON_SQUARED)
    {
        wakeUp();
    }

    this->force += force;
    return this;
}

Rigidbody *Rigidbody::setMass(const float mass)
{
    platformator_detail::nonnegative(mass, "body.mass");
    if (bodyType == DYNAMIC && mass == 0)
        throw std::invalid_argument("Dynamic body mass must be positive");
    this->mass = mass > 0.0f ? mass : 0.0f;
    wakeUp();
    this->inverseMass = this->mass > 0.0f ? 1.0f / this->mass : 0.0f;
    return refreshMomentOfInertiaCache();
}

Rigidbody *Rigidbody::setAngularVelocity(const float angularVelocity)
{
    platformator_detail::finite(angularVelocity, "body.angularVelocity");
    if (getIsSleeping() && std::abs(angularVelocity) > WAKE_ANGULAR_EPSILON)
    {
        wakeUp();
    }

    if (bodyType == BodyType::STATIC)
    {
        return this;
    }

    this->angularVelocity = angularVelocity;
    if (bodyType == KINEMATIC)
        queueParticipationChange();
    return this;
}

Rigidbody *Rigidbody::setTorque(const float torque)
{
    platformator_detail::finite(torque, "body.torque");
    if (getIsSleeping() && std::abs(torque) > WAKE_ANGULAR_EPSILON)
    {
        wakeUp();
    }

    this->torque = torque;
    return this;
}

Rigidbody *Rigidbody::setFriction(const float friction)
{
    platformator_detail::nonnegative(friction, "body.friction");
    this->friction = friction;
    wakeUp();
    return this;
}

Rigidbody *Rigidbody::setRestitution(const float restitution)
{
    platformator_detail::nonnegative(restitution, "body.restitution");
    if (restitution > 1)
        throw std::invalid_argument("body.restitution must be <= 1");
    this->restitution = restitution;
    wakeUp();
    return this;
}

Rigidbody *Rigidbody::setBodyType(const BodyType bodyType)
{
    if (bodyType < DYNAMIC || bodyType > KINEMATIC)
        throw std::invalid_argument("Unknown body type");
    if (bodyType == DYNAMIC)
        platformator_detail::positive(mass, "dynamic mass");
    this->bodyType = bodyType;
    queueParticipationChange();
    if (bodyType != BodyType::DYNAMIC)
    {
        setIsSleeping(false);
    }
    initialize();
    if (auto *object = getGameObject())
        if (auto *collider = object->getComponent<Collider>())
            collider->invalidateResponse();
    return this;
}

Rigidbody *Rigidbody::setGravity(const bool gravity)
{
    this->gravity = gravity;
    wakeUp();
    queueParticipationChange();
    return this;
}

Rigidbody *Rigidbody::applyForces(double timeDelta, const Eigen::Vector2f &gravityVector)
{
    if (bodyType != DYNAMIC || !getGameObject()->getActive() || sleeping)
        return this;
    integrateForces(timeDelta, gravityVector);
    return this;
}

void Rigidbody::integrateForces(double timeDelta, const Eigen::Vector2f &gravityVector)
{
    angularVelocity += torque * timeDelta * inverseMomentOfInertia;
    velocity += timeDelta * (force * inverseMass + (gravity ? gravityVector : Eigen::Vector2f::Zero()));
}

Rigidbody *Rigidbody::move(double timeDelta)
{
    if (bodyType == STATIC || !getGameObject()->getActive() || sleeping)
    {
        force.setZero();
        torque = 0;
        return this;
    }
    const auto changes = integrateLocal(timeDelta);
    if (changes & 1)
        getGameObject()->queueTransform();
    else if (changes & 2)
        getGameObject()->updateCollider();
    return this;
}

uint8_t Rigidbody::integrateLocal(double timeDelta)
{
    this->force = Eigen::Vector2f(0.0f, 0.0f);
    this->torque = 0.0f;

    const bool translated = !velocity.isZero(0), rotated = angularVelocity != 0;
    if (!translated && !rotated)
        return 0;
    auto *object = getGameObject();
    if (translated)
        object->position += velocity * timeDelta;
    if (rotated)
    {
        object->rotation = std::fmod(object->rotation + angularVelocity * timeDelta, 2.0 * M_PI);
        object->sinRotation = std::sin(object->rotation);
        object->cosRotation = std::cos(object->rotation);
    }
    return (translated ? 1 : 0) | (rotated ? 2 : 0);
}

float Rigidbody::getInverseMass() const
{
    if (bodyType != BodyType::DYNAMIC)
    {
        return 0.0f;
    }
    return inverseMass;
}

float Rigidbody::getInverseMomentOfInertia() const
{
    if (bodyType != BodyType::DYNAMIC)
    {
        return 0.0f;
    }

    return inverseMomentOfInertia;
}

Rigidbody *Rigidbody::addImpulse(const Eigen::Vector2f &impulse)
{
    if (!impulse.allFinite())
        throw std::invalid_argument("body.impulse must be finite");
    if (this->getBodyType() != BodyType::DYNAMIC || this->getGameObject()->getActive() == false)
    {
        return this;
    }

    if (getIsSleeping() && impulse.squaredNorm() > WAKE_LINEAR_EPSILON_SQUARED)
    {
        wakeUp();
    }

    this->setVelocity(this->getVelocity() + impulse * this->getInverseMass());
    return this;
}

bool Rigidbody::getIsSleeping() const
{
    return sleeping;
}

Rigidbody *Rigidbody::setIsSleeping(bool sleeping)
{
    if (this->sleeping == sleeping)
    {
        return this;
    }

    this->sleeping = sleeping;
    queueParticipationChange();
    if (sleeping)
    {
        // PLATFORMATOR_LOG("Rigidbody on GameObject '%s' is now sleeping.\n", getGameObject()->getName().c_str());
        velocity = Eigen::Vector2f::Zero();
        force = Eigen::Vector2f::Zero();
        angularVelocity = 0.0f;
        torque = 0.0f;
        sleepTimer = 0.0;
    }
    else
    {
        // PLATFORMATOR_LOG("Rigidbody on GameObject '%s' woke up.\n", getGameObject()->getName().c_str());
        sleepTimer = 0.0;
    }

    return this;
}

void Rigidbody::queueParticipationChange()
{
    // Setters affecting active/sleep lists coalesce here. Awake dynamic solver impulses need no refresh.
    if (isRegisteredInPhysicsManager)
        platformator_detail::RuntimeAccess::gameManager().getPhysicsManager()->queueBodyParticipationChange(this);
}

void Rigidbody::applySolverImpulse(const Eigen::Vector2f &impulse, const Eigen::Vector2f &arm)
{
    if (bodyType != DYNAMIC || sleeping)
        return;
    velocity += impulse * inverseMass;
    angularVelocity += inverseMomentOfInertia * (arm.x() * impulse.y() - arm.y() * impulse.x());
}

Rigidbody *Rigidbody::wakeUp()
{
    return setIsSleeping(false);
}

bool Rigidbody::hasSupportContact() const
{
    return supportContactCount > 0;
}

size_t Rigidbody::getSupportContactCount() const
{
    return supportContactCount;
}

Rigidbody *Rigidbody::addSupportContact()
{
    supportContactCount++;
    return this;
}

Rigidbody *Rigidbody::removeSupportContact()
{
    if (supportContactCount > 0)
    {
        supportContactCount--;
    }
    return this;
}

double Rigidbody::getSleepTimer() const
{
    return sleepTimer;
}

Rigidbody *Rigidbody::setSleepTimer(double sleepTimer)
{
    this->sleepTimer = sleepTimer;
    return this;
}

bool Rigidbody::getIsRegisteredInPhysicsManager() const
{
    return isRegisteredInPhysicsManager;
}

Rigidbody *Rigidbody::setIsRegisteredInPhysicsManager(bool isRegisteredInPhysicsManager)
{
    this->isRegisteredInPhysicsManager = isRegisteredInPhysicsManager;
    return this;
}

size_t Rigidbody::getPhysicsManagerIndex() const
{
    return physicsManagerIndex;
}

Rigidbody *Rigidbody::setPhysicsManagerIndex(size_t physicsManagerIndex)
{
    this->physicsManagerIndex = physicsManagerIndex;
    return this;
}

bool Rigidbody::qualifiesAsSupportContact(const Eigen::Vector2f &contactDirectionNormal,
                                          const Eigen::Vector2f &gravityVectorNormal) const
{
    if (bodyType == BodyType::STATIC)
    {
        return false;
    }

    return contactDirectionNormal.dot(-gravityVectorNormal) >= SUPPORT_NORMAL_THRESHOLD;
}
