#OBJS specifies which files to compile as part of the project
OBJS = main.cpp sdlwindow.h sdlwindow.cpp animator.h animator.cpp audio.h audio.cpp boundingradiousprojection.h boundingradiousprojection.cpp camera.h camera.cpp circlecollider.h circlecollider.cpp collider.h collider.cpp collision.h collision.cpp component.cpp gamemanager.h gamemanager.cpp gameobject.h gameobject.cpp gameobjectmanager.h gameobjectmanager.cpp localsortarray.h localsortarray.cpp physicsmanager.h physicsmanager.cpp rectcollider.h rectcollider.cpp rigidbody.h rigidbody.cpp segmentedintervallist.h segmentedintervallist.cpp sprite.h sprite.cpp

#CC specifies which compiler we're using
CC = g++

#COMPILER_FLAGS specifies the additional compilation options we're using
# -w suppresses all warnings
COMPILER_FLAGS = -w

#LINKER_FLAGS specifies the libraries we're linking against
LINKER_FLAGS = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer

#OBJ_NAME specifies the name of our exectuable
OBJ_NAME = main

#This is the target that compiles our executable
all : $(OBJS)
	$(CC) $(OBJS) $(COMPILER_FLAGS) $(LINKER_FLAGS) -o $(OBJ_NAME)