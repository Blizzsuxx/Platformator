#include <stdio.h>
#include "sdlwindow.h"

int main( int argc, char* args[] )
{
    SDLWindow window;
    if( !window.init() )
    {
        printf( "Failed to initialize!\n" );
    }
    else
    {
        window.loop();
    }

    return 0;
}