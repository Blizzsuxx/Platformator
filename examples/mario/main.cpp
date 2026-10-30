#include "platformator/runner.h"
#include "assets/behaviors/mario_game.h"

int main(int argc, char *args[])
{
    return platformator::run(argc, args, "assets/scenes/mario_example.scene");
}
