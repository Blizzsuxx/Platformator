#include <stdio.h>
#include "gamemanager.h"

int main(int argc, char *args[])
{
    GameManager gameManager = GameManager();
    gameManager.loop();

    return 0;
}