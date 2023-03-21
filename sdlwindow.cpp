#include "sdlwindow.h"

SDLWindow::SDLWindow() : window(NULL), screenSurface(NULL), e(), quit(false)
{
}

SDLWindow::~SDLWindow()
{
    close();
}

bool SDLWindow::init()
{
    //Initialize SDL
    if( SDL_Init( SDL_INIT_VIDEO ) < 0 )
    {
        printf( "SDL could not initialize! SDL_Error: %s", SDL_GetError() );
        return false;
    }

    //Create window
    window = SDL_CreateWindow( "Platformator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN );
    if( window == NULL )
    {
        printf( "Window could not be created! SDL_Error: %s", SDL_GetError() );
        return false;
    }

    //Get window surface
    screenSurface = SDL_GetWindowSurface( window );

    return true;
}

void SDLWindow::close()
{
    //Destroy window
    SDL_DestroyWindow( window );
    window = NULL;
    screenSurface = NULL;

    //Quit SDL subsystems
    SDL_Quit();
}

void SDLWindow::handleEvents()
{
    //Handle events on queue
    while( SDL_PollEvent( &e ) != 0 )
    {
        //User requests quit
        if( e.type == SDL_QUIT )
        {
            quit = true;
        }
    }
}

void SDLWindow::applyPhysics()
{

}

void SDLWindow::resolveCollisions()
{

}

void SDLWindow::render()
{
    //Update the surface
    SDL_UpdateWindowSurface( window );
}

void SDLWindow::loop()
{
    while( !quit )
    {
        handleEvents();
        applyPhysics();
        resolveCollisions();
        render();
    }
}

SDL_Window* SDLWindow::getWindow() const
{
    return window;
}

SDL_Surface* SDLWindow::getSurface() const
{
    return screenSurface;
}
