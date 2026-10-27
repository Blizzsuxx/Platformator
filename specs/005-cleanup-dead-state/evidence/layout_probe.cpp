#include <cstdio>
#include "colliderpair.h"
#include "platformator/collisiontypes.h"
#include "platformator/audio.h"
#include "platformator/gameobject.h"
int main() { std::printf("ColliderPair=%zu ClipPointWithData=%zu Audio=%zu GameObject=%zu\n", sizeof(ColliderPair), sizeof(ClipPointWithData), sizeof(Audio), sizeof(GameObject)); }
