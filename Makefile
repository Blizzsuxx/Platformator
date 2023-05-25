OBJS = animator.cpp animator.h audio.cpp audio.h boundingradiousprojection.cpp camera.cpp camera.h circlecollider.cpp circlecollider.h collider.cpp collider.h collision.cpp collision.h component.cpp gamemanager.cpp gamemanager.h gameobject.cpp gameobject.h gameobjectmanager.cpp gameobjectmanager.h localsortarray.cpp localsortarray.h main.cpp physicsmanager.cpp physicsmanager.h boxcollider.cpp boxcollider.h rigidbody.cpp rigidbody.h sdlwindow.cpp sdlwindow.h segmentedintervallist.cpp segmentedintervallist.h sprite.cpp sprite.h 
CC = g++
COMPILER_FLAGS = -w
LINKER_FLAGS = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer
OBJ_NAME = main

all : $(OBJS)
	$(CC) $(OBJS) $(COMPILER_FLAGS) $(LINKER_FLAGS) -o $(OBJ_NAME)
