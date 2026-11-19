#include "platformator/rigidbody.h"
#include "constants.h"
#include "inputvalidation.h"
#include "objectpool.h"
#include "physicsmanager.h"
#include "platformator/boxcollider.h"
#include "platformator/circlecollider.h"
#include "runtimeaccess.h"

void *Rigidbody::operator new(size_t size)
{
    if (size == sizeof(Rigidbody))
    {
        return platformator_detail::allocatePooledObject(platformator_detail::PooledObjectKind::Rigidbody, size);
    }
    return ::operator new(size);
}

void Rigidbody::operator delete(void *ptr, size_t size) noexcept
{
    if (ptr == nullptr)
    {
        return;
    }
    if (size == sizeof(Rigidbody))
    {
        platformator_detail::deallocatePooledObject(platformator_detail::PooledObjectKind::Rigidbody, ptr, size);
        return;
    }
    ::operator delete(ptr);
}

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
      gravity(gravity), sleeping(false), supportContactCount(0), sleepTimer(0.0),
      isRegisteredInPhysicsManager(false), physicsManagerIndex(SIZE_MAX)
{
    initialize();
}

Rigidbody::~Rigidbody()
{
}

Component *Rigidbody::clone() const
{
    auto cloned = std::make_unique<Rigidbody>();
    cloned->setBodyType(bodyType);
    cloned->setGravity(gravity);
    cloned->setVelocity(velocity);
    cloned->setForce(force);
    cloned->setMass(mass);
    cloned->setAngularVelocity(angularVelocity);
    cloned->setTorque(torque);
    cloned->setFriction(friction);
    cloned->setRestitution(restitution);
    return cloned.release();
}

void Rigidbody::initialize()
{
    if (bodyType < DYNAMIC || bodyType > KINEMATIC)
    {
        throw std::invalid_argument("Unknown body type");
    }
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

// Setters
Rigidbody *Rigidbody::setVelocity(const Eigen::Vector2f &velocity)
{
    if (!velocity.allFinite())
    {
        throw std::invalid_argument("body.velocity must be finite");
    }
    if (getIsSleeping() && velocity.squaredNorm() > WAKE_LINEAR_EPSILON_SQUARED)
    {
        wakeUp();
    }

    if (bodyType == BodyType::STATIC)
    {
        return this;
    }

    if (this->velocity == velocity)
        return this;
    this->velocity = velocity;
    if (bodyType == KINEMATIC)
    {
        queueParticipationChange();
    }
    return this;
}

Rigidbody *Rigidbody::setForce(const Eigen::Vector2f &force)
{
    if (!force.allFinite())
    {
        throw std::invalid_argument("body.force must be finite");
    }
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
    {
        throw std::invalid_argument("body.force must be finite");
    }
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
    {
        throw std::invalid_argument("Dynamic body mass must be positive");
    }
    if (this->mass == mass)
        return this;
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

    if (this->angularVelocity == angularVelocity)
        return this;
    this->angularVelocity = angularVelocity;
    if (bodyType == KINEMATIC)
    {
        queueParticipationChange();
    }
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
    if (this->friction == friction)
        return this;
    this->friction = friction;
    wakeUp();
    return this;
}

Rigidbody *Rigidbody::setRestitution(const float restitution)
{
    platformator_detail::nonnegative(restitution, "body.restitution");
    if (restitution > 1)
    {
        throw std::invalid_argument("body.restitution must be <= 1");
    }
    if (this->restitution == restitution)
        return this;
    this->restitution = restitution;
    wakeUp();
    return this;
}

Rigidbody *Rigidbody::setBodyType(const BodyType bodyType)
{
    if (bodyType < DYNAMIC || bodyType > KINEMATIC)
    {
        throw std::invalid_argument("Unknown body type");
    }
    if (bodyType == DYNAMIC)
    {
        platformator_detail::positive(mass, "dynamic mass");
    }
    this->bodyType = bodyType;
    queueParticipationChange();
    if (bodyType != BodyType::DYNAMIC)
    {
        setIsSleeping(false);
    }
    initialize();
    if (auto *object = getGameObject())
    {
        if (auto *collider = object->getComponent<Collider>())
        {
            collider->invalidateResponse();
        }
    }
    return this;
}

Rigidbody *Rigidbody::setGravity(const bool gravity)
{
    if (this->gravity == gravity)
        return this;
    this->gravity = gravity;
    wakeUp();
    queueParticipationChange();
    return this;
}

void Rigidbody::integrateForces(double timeDelta, const Eigen::Vector2f &gravityVector)
{
    angularVelocity += torque * timeDelta * inverseMomentOfInertia;
    velocity += timeDelta * (force * inverseMass + (gravity ? gravityVector : Eigen::Vector2f::Zero()));
}

uint8_t Rigidbody::integrateLocal(double timeDelta)
{
    this->force = Eigen::Vector2f(0.0f, 0.0f);
    this->torque = 0.0f;

    const bool translated = !velocity.isZero(0), rotated = angularVelocity != 0;
    if (!translated && !rotated)
    {
        return 0;
    }
    auto *object = getGameObject();
    if (translated)
    {
        object->position += velocity * timeDelta;
    }
    if (rotated)
    {
        object->rotation = std::fmod(object->rotation + angularVelocity * timeDelta, 2.0 * M_PI);
        object->sinRotation = std::sin(object->rotation);
        object->cosRotation = std::cos(object->rotation);
    }
    return (translated ? 1 : 0) | (rotated ? 2 : 0);
}

Rigidbody *Rigidbody::addImpulse(const Eigen::Vector2f &impulse)
{
    if (!impulse.allFinite())
    {
        throw std::invalid_argument("body.impulse must be finite");
    }
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
        velocity.setZero();
        force.setZero();
        angularVelocity = 0.0f;
        torque = 0.0f;
    }

    sleepTimer = 0.0;
    return this;
}

void Rigidbody::queueParticipationChange()
{
    // Setters affecting active/sleep lists coalesce here. Awake dynamic solver impulses need no refresh.
    if (isRegisteredInPhysicsManager)
    {
        platformator_detail::RuntimeAccess::physicsManager()->queueBodyParticipationChange(this);
    }
}

void Rigidbody::applySolverImpulse(const Eigen::Vector2f &impulse, const Eigen::Vector2f &arm)
{
    if (bodyType != DYNAMIC || sleeping)
    {
        return;
    }
    velocity += impulse * inverseMass;
    angularVelocity += inverseMomentOfInertia * (arm.x() * impulse.y() - arm.y() * impulse.x());
}

Rigidbody *Rigidbody::wakeUp()
{
    return setIsSleeping(false);
}

Rigidbody *Rigidbody::setSleepTimer(double sleepTimer)
{
    this->sleepTimer = sleepTimer;
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
