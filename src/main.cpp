#include "platformator/runner.h"

int main(int argc, char *args[])
{
    return platformator::run(argc, args, "assets/scenes/default.scene");
}