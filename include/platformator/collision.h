#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "platformator/collider.h"

class PhysicsManager;

class Collision
{
	friend class PhysicsManager;

public:
	enum CollisionType
	{
		DYNAMIC,
		KINEMATIC,
		TRIGGER,
		NONE_COLLISIONS
	};

	Collision();
	Collision(Collider *colliderA, Collider *colliderB);
	Collision(const Eigen::Vector2f &normal, float penetration, const ClipPointsWithData &contactPoints, Collider *colliderA, Collider *colliderB);
	~Collision();

	const Eigen::Vector2f &getNormal() const;
	ClipPointsWithData &getContactPoints() const;
	float getFriction() const;
	float getRestitution() const;

	const Collider *getReferenceObject() const;
	const Collider *getIncidentObject() const;

	CollisionType getResolutionType() const;
	void updateSupportState(const Eigen::Vector2f &gravityVector) const;
	void clearSupportState() const;

	void setNormal(const Eigen::Vector2f &normal) const;
	void setContactPoints(const ClipPoints &contactPoints) const;
	void setReferenceObject(const Collider *colliderA) const;
	void setIncidentObject(const Collider *colliderB) const;

	bool isVerticalCollision() const;
	bool isHorizontalCollision() const;

	bool operator==(const Collision &other) const;
	bool operator!=(const Collision &other) const;

	class HashFunction
	{
	public:
		size_t operator()(const Collision &collision) const
		{
			const Collider *objectA = collision.getReferenceObject();
			const Collider *objectB = collision.getIncidentObject();

			if (objectA > objectB)
			{
				std::swap(objectA, objectB);
			}

			size_t hash1 = std::hash<const Collider *>()(objectA);
			size_t hash2 = std::hash<const Collider *>()(objectB);
			return hash1 ^ (hash2 << 1);
		}
	};

	enum SupportState : uint8_t
	{
		NONE_SUPPORT = 0,
		SUPPORTS_REFERENCE = 1 << 0,
		SUPPORTS_INCIDENT = 1 << 1,
		SUPPORTS_BOTH = SUPPORTS_REFERENCE | SUPPORTS_INCIDENT
	};

private:
	mutable Eigen::Vector2f normal;
	mutable ClipPointsWithData contactPoints;
	mutable const Collider *referenceObject;
	mutable const Collider *incidentObject;
	mutable uint8_t supportState;

	bool getSupportsReferenceBody() const;
	bool getSupportsIncidentBody() const;
	void setSupportsReferenceBody(bool supports) const;
	void setSupportsIncidentBody(bool supports) const;
	void generateFallbackContactPoint();
};