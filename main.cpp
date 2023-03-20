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
        //Apply the image
        SDL_BlitSurface( window.getSurface(), NULL, window.getSurface(), NULL );
        //Update the surface
        SDL_UpdateWindowSurface( window.getWindow() );
        //Wait two seconds
        SDL_Delay( 2000 );
    }

    //Free resources and close SDL
    window.close();

    return 0;
}