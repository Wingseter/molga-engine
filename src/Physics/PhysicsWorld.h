#pragma once

#include <set>
#include <map>
#include "../Common/Types.h"

class World;
class GameObject;

class PhysicsWorld {
public:
    struct ContactKey {
        unsigned int idA;
        unsigned int idB;
        bool isTrigger;

        bool operator<(const ContactKey& o) const {
            if (idA != o.idA) return idA < o.idA;
            if (idB != o.idB) return idB < o.idB;
            return isTrigger < o.isTrigger;
        }
    };

    PhysicsWorld();
    ~PhysicsWorld();

    void Step(World& world, float fixedDt);

private:
    std::set<ContactKey> previousContacts;
};
